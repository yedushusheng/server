/*****************************************************************************

Copyright (c) 2020, 2021, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

#include "srw_lock.h"
#include "srv0srv.h"
#include "my_cpu.h"

#ifdef SRW_LOCK_DUMMY
void ssux_lock_low::init()
{
  DBUG_ASSERT(!is_locked_or_waiting());
  pthread_mutex_init(&mutex, nullptr);
  pthread_cond_init(&cond_shared, nullptr);
  pthread_cond_init(&cond_exclusive, nullptr);
}

void ssux_lock_low::destroy()
{
  DBUG_ASSERT(!is_locked_or_waiting());
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond_shared);
  pthread_cond_destroy(&cond_exclusive);
}

inline void ssux_lock_low::writer_wait(uint32_t l)
{
  pthread_mutex_lock(&mutex);
  while (value() == l)
    pthread_cond_wait(&cond_exclusive, &mutex);
  pthread_mutex_unlock(&mutex);
}

inline void ssux_lock_low::readers_wait(uint32_t l)
{
  pthread_mutex_lock(&mutex);
  while (value() == l)
    pthread_cond_wait(&cond_shared, &mutex);
  pthread_mutex_unlock(&mutex);
}

inline void ssux_lock_low::wake()
{
  pthread_mutex_lock(&mutex);
  uint32_t l= value();
  if (l & WRITER)
    DBUG_ASSERT(!(l & ~WRITER_PENDING));
  else
  {
    pthread_cond_broadcast(&cond_exclusive);
    if (!(l & WRITER_PENDING))
      pthread_cond_broadcast(&cond_shared);
  }
  pthread_mutex_unlock(&mutex);
}

/** Wait for a read lock.
@param lock word value from a failed read_trylock() */
void ssux_lock_low::read_lock(uint32_t l)
{
  do
  {
    if (l == WRITER_WAITING)
    {
    wake_writer:
      pthread_mutex_lock(&mutex);
      for (;;)
      {
        if (l == WRITER_WAITING)
          pthread_cond_signal(&cond_exclusive);
        l= value();
        if (!(l & WRITER_PENDING))
          break;
        pthread_cond_wait(&cond_shared, &mutex);
      }
      pthread_mutex_unlock(&mutex);
      continue;
    }
    else
      for (auto spin= srv_n_spin_wait_rounds; spin; spin--)
      {
        ut_delay(srv_spin_wait_delay);
        if (read_trylock<true>(l))
          return;
        else if (l == WRITER_WAITING)
          goto wake_writer;
      }

    readers_wait(l);
  }
  while (!read_trylock<true>(l));
}

/** Wait for an update lock.
@param lock word value from a failed update_trylock() */
void ssux_lock_low::update_lock(uint32_t l)
{
  do
  {
    if (l == WRITER_WAITING)
    {
    wake_writer:
      pthread_mutex_lock(&mutex);
      for (;;)
      {
        if (l == WRITER_WAITING)
          pthread_cond_signal(&cond_exclusive);
        l= value();
        if (!(l & WRITER_PENDING))
          break;
        pthread_cond_wait(&cond_shared, &mutex);
      }
      pthread_mutex_unlock(&mutex);
      continue;
    }
    else
      for (auto spin= srv_n_spin_wait_rounds; spin; spin--)
      {
        ut_delay(srv_spin_wait_delay);
        if (update_trylock(l))
          return;
        else if (l == WRITER_WAITING)
          goto wake_writer;
      }

    readers_wait(l);
  }
  while (!update_trylock(l));
}

/** Wait for a write lock after a failed write_trylock() or upgrade_trylock()
@param holding_u  whether we already hold u_lock() */
void ssux_lock_low::write_lock(bool holding_u)
{
  for (;;)
  {
    uint32_t l= write_lock_wait_start();
    /* We are the first writer to be granted the lock. Spin for a while. */
    for (auto spin= srv_n_spin_wait_rounds; spin; spin--)
    {
      l= holding_u ? WRITER_WAITING | UPDATER : WRITER_WAITING;
      if (write_lock_wait_try(l))
        return;
      if (!(l & WRITER_WAITING))
        l= write_lock_wait_start();
      ut_delay(srv_spin_wait_delay);
    }

    const uint32_t e= holding_u ? WRITER_WAITING | UPDATER : WRITER_WAITING;
    l= e;
    if (write_lock_wait_try(l))
      return;

    if (!(l & WRITER_WAITING))
    {
      switch (l) {
      case UNLOCKED:
        DBUG_ASSERT(!holding_u);
        if (write_trylock())
          return;
        break;
      case UPDATER:
        if (holding_u && upgrade_trylock())
          return;
      }

      for (l= write_lock_wait_start() | WRITER_WAITING;
           (l | WRITER_WAITING) == e; )
        if (write_lock_wait_try(l))
          return;
    }
    else
      DBUG_ASSERT(~WRITER_WAITING & l);

    writer_wait(l);
  }
}

void ssux_lock_low::rd_unlock() { if (read_unlock()) wake(); }
void ssux_lock_low::u_unlock() { update_unlock(); wake(); }
void ssux_lock_low::wr_unlock() { write_unlock(); wake(); }
#else /* SRW_LOCK_DUMMY */
static_assert(4 == sizeof(rw_lock), "ABI");
# ifdef _WIN32
#  include <synchapi.h>

inline void srw_mutex::wait(uint32_t l)
{ WaitOnAddress(&lock, &l, 4, INFINITE); }
void srw_mutex::wake() { WakeByAddressSingle(&lock); }

inline void ssux_lock_low::wait(uint32_t l)
{ WaitOnAddress(&readers, &l, 4, INFINITE); }
void ssux_lock_low::wake() { WakeByAddressSingle(&readers); }

# else
#  ifdef __linux__
#   include <linux/futex.h>
#   include <sys/syscall.h>
#   define SRW_FUTEX(a,op,n) \
    syscall(SYS_futex, a, FUTEX_ ## op ## _PRIVATE, n, nullptr, nullptr, 0)
#  elif defined __OpenBSD__
#   include <sys/time.h>
#   include <sys/futex.h>
#   define SRW_FUTEX(a,op,n) \
    futex((volatile uint32_t*) a, FUTEX_ ## op, n, nullptr, nullptr)
#  else
#   error "no futex support"
#  endif

inline void srw_mutex::wait(uint32_t l) { SRW_FUTEX(&lock, WAIT, l); }
void srw_mutex::wake() { SRW_FUTEX(&lock, WAKE, 1); }

inline void ssux_lock_low::wait(uint32_t l) { SRW_FUTEX(&readers, WAIT, l); }
void ssux_lock_low::wake() { SRW_FUTEX(&readers, WAKE, 1); }

# endif


void srw_mutex::wait_and_lock()
{
  uint32_t l= 1 + lock.fetch_add(1, std::memory_order_relaxed);
  for (auto spin= srv_n_spin_wait_rounds; spin; spin--)
  {
    l&= ~HOLDER;
    DBUG_ASSERT(l);
    while (!lock.compare_exchange_weak(l, HOLDER | (l - 1),
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed))
      if (l & HOLDER)
        goto occupied;
    return;
occupied:
    ut_delay(srv_spin_wait_delay);
  }

  for (;;)
  {
    l= lock.load(std::memory_order_relaxed);
    while (!(l & HOLDER))
    {
      DBUG_ASSERT(l);
      if (lock.compare_exchange_weak(l, HOLDER | (l - 1),
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed))
        return;
    }
    DBUG_ASSERT(l > HOLDER);
    wait(l);
  }
}

void ssux_lock_low::wr_wait(uint32_t l)
{
  DBUG_ASSERT(writer.is_locked());
  DBUG_ASSERT(l);
  DBUG_ASSERT(l < WRITER);
  l|= WRITER;
  do
  {
    DBUG_ASSERT(l > WRITER);
    wait(l);
    l= readers.load(std::memory_order_acquire);
  }
  while (l != WRITER);
}

void ssux_lock_low::rd_wait()
{
  for (;;)
  {
    writer.wr_lock();
    uint32_t l= readers.fetch_add(1, std::memory_order_acquire);
    if (UNIV_UNLIKELY(l == WRITER))
    {
      readers.fetch_sub(1, std::memory_order_relaxed);
      wake();
      writer.wr_unlock();
      pthread_yield();
      continue;
    }
    DBUG_ASSERT(!(l & WRITER));
    break;
  }
  writer.wr_unlock();
}
#endif /* SRW_LOCK_DUMMY */

#ifdef UNIV_PFS_RWLOCK
void srw_lock::psi_rd_lock(const char *file, unsigned line)
{
  PSI_rwlock_locker_state state;
  const bool nowait= lock.rd_lock_try();
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_rdwait)
      (&state, pfs_psi,
       nowait ? PSI_RWLOCK_TRYREADLOCK : PSI_RWLOCK_READLOCK, file, line))
  {
    if (!nowait)
      lock.rd_lock();
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
  else if (!nowait)
    lock.rd_lock();
}

void srw_lock::psi_wr_lock(const char *file, unsigned line)
{
  PSI_rwlock_locker_state state;
  const bool nowait= lock.wr_lock_try();
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_wrwait)
      (&state, pfs_psi,
       nowait ? PSI_RWLOCK_TRYWRITELOCK : PSI_RWLOCK_WRITELOCK, file, line))
  {
    if (!nowait)
      lock.wr_lock();
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
  else if (!nowait)
    lock.wr_lock();
}

void ssux_lock::psi_rd_lock(const char *file, unsigned line)
{
  PSI_rwlock_locker_state state;
  const bool nowait= lock.rd_lock_try();
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_rdwait)
      (&state, pfs_psi,
       nowait ? PSI_RWLOCK_TRYSHAREDLOCK : PSI_RWLOCK_SHAREDLOCK, file, line))
  {
    if (!nowait)
      lock.rd_lock();
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
  else if (!nowait)
    lock.rd_lock();
}

void ssux_lock::psi_u_lock(const char *file, unsigned line)
{
  PSI_rwlock_locker_state state;
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_wrwait)
      (&state, pfs_psi, PSI_RWLOCK_SHAREDEXCLUSIVELOCK, file, line))
  {
    lock.u_lock();
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
  else
    lock.u_lock();
}

void ssux_lock::psi_wr_lock(const char *file, unsigned line)
{
  PSI_rwlock_locker_state state;
  const bool nowait= lock.wr_lock_try();
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_wrwait)
      (&state, pfs_psi,
       nowait ? PSI_RWLOCK_TRYEXCLUSIVELOCK : PSI_RWLOCK_EXCLUSIVELOCK,
       file, line))
  {
    if (!nowait)
      lock.wr_lock();
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
  else if (!nowait)
    lock.wr_lock();
}

void ssux_lock::psi_u_wr_upgrade(const char *file, unsigned line)
{
  PSI_rwlock_locker_state state;
# ifdef SRW_LOCK_DUMMY
  const bool nowait= lock.upgrade_trylock();
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_wrwait)
      (&state, pfs_psi,
       nowait ? PSI_RWLOCK_TRYEXCLUSIVELOCK : PSI_RWLOCK_EXCLUSIVELOCK,
       file, line))
  {
    if (!nowait)
      lock.write_lock(true);
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
# else /* SRW_LOCK_DUMMY */
  DBUG_ASSERT(lock.writer.is_locked());
  uint32_t l= 1;
  const bool nowait=
    lock.readers.compare_exchange_strong(l, ssux_lock_low::WRITER,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed);
  if (PSI_rwlock_locker *locker= PSI_RWLOCK_CALL(start_rwlock_wrwait)
      (&state, pfs_psi,
       nowait ? PSI_RWLOCK_TRYEXCLUSIVELOCK : PSI_RWLOCK_EXCLUSIVELOCK,
       file, line))
  {
    if (!nowait)
      lock.u_wr_upgrade();
    PSI_RWLOCK_CALL(end_rwlock_rdwait)(locker, 0);
  }
# endif /* SRW_LOCK_DUMMY */
  else if (!nowait)
    lock.u_wr_upgrade();
}
#endif /* UNIV_PFS_RWLOCK */
