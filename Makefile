
MODULES = period
DATA_built = period.sql
DATA = uninstall_period.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

