

/***************************************************************************
 List all privileges supported
***************************************************************************/

struct show_privileges_st {
  unsigned version;
  const char *privilege;
  const char *context;
  const char *comment;
};

static struct show_privileges_st sys_privileges[]=
{
  {0, "Alter", "Tables",  "To alter the table"},
  {0, "Alter routine", "Functions,Procedures",  "To alter or drop stored functions/procedures"},
  {0, "Create", "Databases,Tables,Indexes",  "To create new databases and tables"},
  {0, "Create routine","Databases","To use CREATE FUNCTION/PROCEDURE"},
  {0, "Create temporary tables","Databases","To use CREATE TEMPORARY TABLE"},
  {0, "Create view", "Tables",  "To create new views"},
  {0, "Create user", "Server Admin",  "To create new users"},
  {0, "Delete", "Tables",  "To delete existing rows"},
  {100304, "Delete history", "Tables", "To delete versioning table historical rows"},
  {0, "Drop", "Databases,Tables", "To drop databases, tables, and views"},
#ifdef HAVE_EVENT_SCHEDULER
  {0, "Event","Server Admin","To create, alter, drop and execute events"},
#endif
  {0, "Execute", "Functions,Procedures", "To execute stored routines"},
  {0, "File", "File access on server",   "To read and write files on the server"},
  {0, "Grant option",  "Databases,Tables,Functions,Procedures", "To give to other users those privileges you possess"},
  {0, "Index", "Tables",  "To create or drop indexes"},
  {0, "Insert", "Tables",  "To insert data into tables"},
  {0, "Lock tables","Databases","To use LOCK TABLES (together with SELECT privilege)"},
  {0, "Process", "Server Admin", "To view the plain text of currently executing queries"},
  {0, "Proxy", "Server Admin", "To make proxy user possible"},
  {0, "References", "Databases,Tables", "To have references on tables"},
  {0, "Reload", "Server Admin", "To reload or refresh tables, logs and privileges"},
  {100502, "Binlog admin", "Server", "To purge binary logs"},
  /* Replication Client replaced iwht binlog monitor in 10.5.2 */
  {0, "Replication client","Server Admin","To ask where the slave or master servers are"},
  {100502, "Binlog monitor", "Server", "To use SHOW BINLOG STATUS and SHOW BINARY LOG"},
  {100502, "Replication master admin", "Server", "To monitor connected slaves"},
  {100502, "Replication slave admin", "Server", "To start/stop slave and apply binlog events"},
  {100508, "Slave monitor", "Server", "To use SHOW SLAVE STATUS and SHOW RELAYLOG EVENTS"},
  {0, "Replication slave","Server Admin","To read binary log events from the master"},
  {0, "Select", "Tables",  "To retrieve rows from table"},
  {0, "Show databases","Server Admin","To see all databases with SHOW DATABASES"},
  {0, "Show view","Tables","To see views with SHOW CREATE VIEW"},
  {0, "Shutdown","Server Admin", "To shut down the server"},
  {0, "Super","Server Admin","To use KILL thread, SET GLOBAL, CHANGE MASTER, etc."},
  {0, "Trigger","Tables", "To use triggers"},
  {0, "Create tablespace", "Server Admin", "To create/alter/drop tablespaces"},
  {0, "Update", "Tables",  "To update existing rows"},
  {100502, "Set user","Server", "To create views and stored routines with a different definer"},
  {100502, "Federated admin", "Server", "To execute the CREATE SERVER, ALTER SERVER, DROP SERVER statements"},
  {100502, "Connection admin", "Server", "To bypass connection limits and kill other users' connections"},
  {100502, "Read_only admin", "Server", "To perform write operations even if @@read_only=ON"},
  {0, "Usage","Server Admin","No privileges - allow connect only"},
  {0, NullS, NullS, NullS}
};
