
CREATE TABLE r1_since (
  k          text primary key,
  att1       text,
  since      t_point,
  log_since  t_point
);
CREATE TABLE r1_during (
  k          text,
  att1       text,
  during     t_interval,
  log_since  t_point
);
CREATE TABLE r1_since_log (
  k          text,
  att1       text,
  since      t_point,
  log_during t_interval
);
CREATE TABLE r1_during_log (
  k          text,
  att1       text,
  during     t_interval,
  log_during t_interval
);


CREATE TABLE r2_since (
  k          text primary key,
  att2       text,
  since      t_point,
  log_since  t_point
);
CREATE TABLE r2_during (
  k          text,
  att2       text,
  during     t_interval,
  log_since  t_point
);
CREATE TABLE r2_since_log (
  k          text,
  att2       text,
  since      t_point,
  log_during t_interval
);
CREATE TABLE r2_during_log (
  k          text,
  att2       text,
  during     t_interval,
  log_during t_interval
);


CREATE VIEW r_since AS SELECT k, att1, att2, since, log_since FROM r1_since NATURAL JOIN r2_since;

CREATE VIEW r_since_log AS SELECT k, att1, att2, since, log_during FROM r1_since_log NATURAL JOIN r2_since_log;

CREATE VIEW r_during AS SELECT k, att1, att2, during, log_since FROM r1_during NATURAL JOIN r2_during;

CREATE VIEW r_during_log AS SELECT k, att1, att2, during, log_during FROM r1_during_log NATURAL JOIN r2_during_log;

CREATE VIEW r AS SELECT k, att1, att2 FROM r_since;

CREATE OR REPLACE FUNCTION r1_since_ins_trfn() RETURNS TRIGGER
  LANGUAGE plpgsql VOLATILE AS $$
BEGIN


$$;













