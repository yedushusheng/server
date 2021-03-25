/* Copyright (C) 2021, MariaDB Corporation.

This program is free software; you can redistribute itand /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111 - 1301 USA*/

#include "tpool_structs.h"
#include "tpool.h"
#include "mysql/service_my_print_error.h"
#include "mysqld_error.h"

#include <liburing.h>

#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>

namespace
{

class aio_uring final : public tpool::aio
{
public:
  aio_uring(tpool::thread_pool *tpool, int max_aio) : tpool_(tpool)
  {
    struct io_uring_params params;
    std::memset(&params, 0, sizeof params);
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000;

    if (io_uring_queue_init_params(max_aio, &uring_, &params) != 0)
    {
      switch (const auto e= errno) {
      case ENOMEM:
      case ENOSYS:
        my_printf_error(ER_UNKNOWN_ERROR, e == ENOMEM
                        ? "io_uring_queue_init() failed with ENOMEM:"
                        " try larger ulimit -l\n"
                        : "io_uring_queue_init() failed with ENOSYS:"
                        " try uprading the kernel\n",
                        ME_ERROR_LOG | ME_WARNING);
        break;
      default:
        my_printf_error(ER_UNKNOWN_ERROR,
                        "io_uring_queue_init() failed with errno %d\n",
                        ME_ERROR_LOG | ME_WARNING, e);
      }
      throw std::runtime_error("aio_uring()");
    }

    thread_= std::thread(thread_routine, this);
  }

  ~aio_uring() noexcept
  {
    shutting_down_= true;
    thread_.join();
    io_uring_queue_exit(&uring_);
  }

  int submit_io(tpool::aiocb *cb) final
  {
    cb->iov_base= cb->m_buffer;
    cb->iov_len= cb->m_len;

    // The whole operation since io_uring_get_sqe() and till io_uring_submit()
    // must be atomical. This is because liburing provides thread-unsafe calls.
    std::lock_guard<std::mutex> _(mutex_);

    io_uring_sqe *sqe= io_uring_get_sqe(&uring_);
    if (cb->m_opcode == tpool::aio_opcode::AIO_PREAD)
      io_uring_prep_readv(sqe, cb->m_fh, static_cast<struct iovec *>(cb), 1,
                          cb->m_offset);
    else
      io_uring_prep_writev(sqe, cb->m_fh, static_cast<struct iovec *>(cb), 1,
                           cb->m_offset);
    io_uring_sqe_set_data(sqe, cb);
    sqe->flags |= IOSQE_FIXED_FILE;

    return io_uring_submit(&uring_) == 1 ? 0 : -1;
  }

  int bind(native_file_handle &fd) final
  {
    std::lock_guard<std::mutex> _(mutex_);

    if (registered_count_)
    {
      if (auto ret= io_uring_unregister_files(&uring_))
      {
        fprintf(stderr, "io_uring_unregister_files()1 returned errno %d",
                -ret);
        abort();
      }
    }

    if (fd >= files_.size())
      files_.resize(fd + 1, -1);

    files_[fd]= fd;
    registered_count_++;

    if (auto ret=
            io_uring_register_files(&uring_, files_.data(), files_.size()))
    {
      fprintf(stderr, "io_uring_register_files()1 returned errno %d", -ret);
      abort();
    }

    return 0;
  }

  int unbind(const native_file_handle &fd) final
  {
    std::lock_guard<std::mutex> _(mutex_);

    assert(registered_count_ > 0);

    if (auto ret= io_uring_unregister_files(&uring_))
    {
      fprintf(stderr, "io_uring_unregister_files()2 returned errno %d", -ret);
      abort();
    }

    assert(fd < files_.size());

    files_[fd]= -1;
    registered_count_--;

    if (registered_count_ > 0)
    {
      if (auto ret=
              io_uring_register_files(&uring_, files_.data(), files_.size()))
      {
        fprintf(stderr, "io_uring_register_files()2 returned errno %d", -ret);
        abort();
      }
    }

    return 0;
  }

private:
  static void thread_routine(aio_uring *aio)
  {
    for (;;)
    {
      io_uring_cqe *cqe;
      {
        std::lock_guard<std::mutex> _(aio->mutex_);

        __kernel_timespec ts{0, 10000000};
        if (int ret= io_uring_wait_cqe_timeout(&aio->uring_, &cqe, &ts))
        {
          if (aio->shutting_down_.load(std::memory_order_relaxed))
            break;

          if (ret == -EINTR) // this may occur during shutdown
            break;

          if (ret == -EAGAIN)
            continue;

          my_printf_error(ER_UNKNOWN_ERROR,
                          "io_uring_peek_cqe() returned %d\n",
                          ME_ERROR_LOG | ME_FATAL, ret);
          abort();
        }
      }

      auto *iocb= static_cast<tpool::aiocb*>(io_uring_cqe_get_data(cqe));
      assert(iocb);

      int res= cqe->res;
      if (res < 0)
      {
        iocb->m_err= -res;
        iocb->m_ret_len= 0;

        fprintf(stderr, "io_uring_peek_cqe() operation returned %d\n", -res);
        abort();
      }
      else
      {
        iocb->m_err= 0;
        iocb->m_ret_len= res;
      }

      io_uring_cqe_seen(&aio->uring_, cqe);

      iocb->m_internal_task.m_func= iocb->m_callback;
      iocb->m_internal_task.m_arg= iocb;
      iocb->m_internal_task.m_group= iocb->m_group;
      aio->tpool_->submit_task(&iocb->m_internal_task);
    }
  }

  io_uring uring_;
  std::mutex mutex_;
  tpool::thread_pool *tpool_;
  std::thread thread_;
  std::atomic<bool> shutting_down_{false};

  std::vector<native_file_handle> files_;
  size_t registered_count_= 0;
};

} // namespace

namespace tpool
{

aio *create_linux_aio(thread_pool *pool, int max_aio)
{
  try {
    return new aio_uring(pool, max_aio);
  } catch (std::runtime_error& error) {
    return nullptr;
  }
}

} // namespace tpool
