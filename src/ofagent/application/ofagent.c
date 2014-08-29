/*********************************************************************
*
* (C) Copyright Broadcom Corporation 2013-2014
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
**********************************************************************
*
* @filename     ofagent.c
*
* @purpose      OF-DPA Agent Program
*
* @component    OF-DPA
*
* @comments     none
*
* @create       23 Oct 2013
*
* @end
*
**********************************************************************/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <libgen.h>
#include <signal.h>
#include <linux/kernel.h>
#include <arpa/inet.h>
#include <argp.h>
#include <regex.h>

#include "ofdpa_util.h"

#include "ofdpa_api.h"
#include <SocketManager/socketmanager.h>
#include <AIM/aim.h>
#include <AIM/aim_pvs_syslog.h>
#include <BigList/biglist.h>
#include <indigo/port_manager.h>
#include <indigo/types.h>
#include <indigo/of_state_manager.h>
#include <SocketManager/socketmanager.h>
#include <OFConnectionManager/ofconnectionmanager.h>
#include <OFStateManager/ofstatemanager.h>
#include <indigo/forwarding.h>
#include <ind_ofdpa_util.h>

#define PIDFILE "/var/run/ofagent/.pid"

#define OFAGENT_VERSION              1.0

#define AIM_LOG_MODULE_NAME ofagent

#include <AIM/aim_log.h>

AIM_LOG_STRUCT_DEFINE(
                      AIM_LOG_OPTIONS_DEFAULT,
                      AIM_LOG_BITS_DEFAULT,
                      NULL,     /* Custom Log Map */
                      0
                      );

#define INDIGO_LOGLEVEL_DEFAULT 0
#define INDIGO_LOGLEVEL_VERBOSE 1
#define INDIGO_LOGLEVEL_TRACE   2

int ofagent_of_version = OF_VERSION_1_3;
const char *argp_program_version;

static ind_soc_config_t soc_cfg;
static ind_cxn_config_t cxn_cfg;
static ind_core_config_t core_cfg;

static int sighup_eventfd;
static int sigterm_eventfd;

static biglist_t *controllers = NULL;
static biglist_t *listeners = NULL;

typedef struct
{
  int           agentdebuglvl;
#ifdef OFAGENT_APP
  int           debuglvl;
  int           debugComps[10]; // 10: TODO: update from OF Agent debug levels
#endif
  of_dpid_t     dpid;
} arguments_t;

/* The options we understand. */
static struct argp_option options[] =
{
  { "agentdebuglvl", 'a', "AGENTDEBUGLVL",  0, "The verbosity of OF Agent debug messages.",                             0 },
#ifdef OFAGENT_APP
  { "ofdpadebuglvl", 'd', "OFDPADEBUGLVL",  0, "The verbosity of OF-DPA debug messages.",                             0 },
  { "ofdpadebugcomp",'c', "OFPDACOMPONENT", 0, "The OF-DPA component for which debug messages are enabled.",          0 },
#endif /* OFAGENT_APP */
  { "controller", 't', "IP:PORT", 0,  "Controller" },
  { "listen",   'l',  "IP:PORT", 0,  "Listen" },
  { "dpid", 'i',  "DATAPATHID", 0,  "Specify Datapath ID." },
  { 0 }
};

extern void __ofstatemanager_module_init__(void);
extern void __biglist_module_init__(void);
extern void __configuration_module_init__(void);
extern void __ofconnectionmanager_module_init__(void);
extern void __socketmanager_module_init__(void);
extern void __ind_ofdpa_driver_module_init__(void);

static int
parse_controller(const char *str,
                 indigo_cxn_protocol_params_t *_proto,
                 int default_port)
{
  char buf[128];
  char *strtok_state = NULL;
  char *ip, *port_str;
  indigo_cxn_params_tcp_over_ipv4_t *proto = &_proto->tcp_over_ipv4;
  struct sockaddr_in sa;

  strncpy(buf, str, sizeof(buf));
  strtok_state = buf;

  proto->protocol = INDIGO_CXN_PROTO_TCP_OVER_IPV4;

  ip = strtok_r(NULL, ":/", &strtok_state);
  if (ip == NULL) {
      AIM_LOG_ERROR("Controller spec \"%s\" missing IP address", str);
      return -1;
  } else if (inet_pton(AF_INET, ip, &sa) != 1) {
      AIM_LOG_ERROR("Could not parse IP address \"%s\"", ip);
      return -1;
  } else {
      strncpy(proto->controller_ip, ip, sizeof(proto->controller_ip));
  }

  port_str = strtok_r(NULL, ":/", &strtok_state);
  if (port_str == NULL) {
      proto->controller_port = default_port;
  } else {
      char *endptr;
      long port = strtol(port_str, &endptr, 0);
      if (*port_str == '\0' || *endptr != '\0') {
          AIM_LOG_ERROR("Could not parse port \"%s\"", port_str);
          return -1;
      } else if (port <= 0 || port > 65535) {
          AIM_LOG_ERROR("Invalid port \"%s\"", port_str);
          return -1;
      } else {
          proto->controller_port = atoi(port_str);
      }
  }

  return 0;
}

static void
sighup_callback(int socket_id, void *cookie,
                int read_ready, int write_ready, int error_seen)
{
    uint64_t x;
    if (read(sighup_eventfd, &x, sizeof(x)) < 0) {
        /* silence warn_unused_result */
    }
    AIM_LOG_MSG("Received SIGHUP");
}

static void
sighup(int signum)
{
    uint64_t x = 1;
    if (write(sighup_eventfd, &x, sizeof(x)) < 0) {
        /* silence warn_unused_result */
    }
}

static void
sigterm_callback(int socket_id, void *cookie,
                 int read_ready, int write_ready, int error_seen)
{
    uint64_t x;
    if (read(sigterm_eventfd, &x, sizeof(x)) < 0) {
        /* silence warn_unused_result */
    }

    ind_soc_run_status_set(IND_SOC_RUN_STATUS_EXIT);
}

static void
sigterm(int signum)
{
    uint64_t x = 1;
    if (write(sigterm_eventfd, &x, sizeof(x)) < 0) {
        /* silence warn_unused_result */
    }
}

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
  /* Get the INPUT argument from `argp_parse', which we
     know is a pointer to our arguments structure. */

  arguments_t *arguments = state->input;
#ifdef OFAGENT_APP
  int          component;
#endif
  switch (key)
  {
#ifdef OFAGENT_APP
    case 'c':                           /* debugcomp */
      errno = 0;

      component = strtoul(arg, NULL, 0);
      if ((errno != 0) ||
          (component >= OFDPA_COMPONENT_MAX))
      {
        argp_error(state, "Invalid component ID \"%s\"", arg);
        return errno;
      }
      arguments->debugComps[component] = 1;

      break;

    case 'd':                           /* debuglvl */
      errno = 0;

      arguments->debuglvl = strtoul(arg, NULL, 0);
      if (errno != 0)
      {
        argp_error(state, "Invalid debuglvl \"%s\"", arg);
        return errno;
      }

      break;
#endif
    case 'a':                           /* ofagent debuglvl */
      errno = 0;

      arguments->agentdebuglvl = strtoul(arg, NULL, 0);
      if (errno != 0)
      {
        argp_error(state, "Invalid agentdebuglvl \"%s\"", arg);
        return errno;
      }

    break;

    case 't':                           /* controller */
      errno = 0;
      controllers = biglist_append(controllers, arg);
      break;

    case 'l':                           /* listen */
      errno = 0;
      listeners = biglist_append(listeners, arg);
      break;

    case 'i':                           /* dpid */
      errno = 0;

      char *endptr;
      arguments->dpid = strtoull(arg, &endptr, 16);
      if (errno != 0)
      {
        argp_error(state, "Invalid dpid \"%s\", must be hex digit string", arg);
        return errno;
      }

    break;

    case ARGP_KEY_NO_ARGS:
    case ARGP_KEY_END:
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static int is_already_running(char *programName)
{
  char *piddir, *pidrundir;
  FILE *pidfile;
  char buf[20];
  pid_t mypid = getpid();
  pid_t runningpid;

  piddir = dirname(strdup(PIDFILE));
  pidrundir = dirname(strdup(piddir));

  errno = 0;
  if ((0 != mkdir(pidrundir, S_IRWXU | S_IRWXG | S_IRWXO)) &&
      (EEXIST != errno))
  {
    printf("%s: Failed to create directory %s for PID file; %s\r\n",
           programName, pidrundir, strerror(errno));
    return 1;
  }

  errno = 0;
  if ((0 != mkdir(piddir, S_IRWXU | S_IRWXG | S_IRWXO)) &&
      (EEXIST != errno))
  {
    printf("%s: Failed to create directory %s for PID file; %s\r\n",
           programName, piddir, strerror(errno));
    return 1;
  }

  errno = 0;
  pidfile = fopen(PIDFILE, "r+");
  if (NULL == pidfile)
  {
    /* If the pidfile does not exist, create it. */
    if (ENOENT == errno)
    {
      errno = 0;
      pidfile = fopen(PIDFILE, "w");
      if (NULL == pidfile)
      {
        printf("%s: Failed to create PID file %s; %s\r\n",
               programName, PIDFILE, strerror(errno));
        return 1;
      }
      fprintf(pidfile, "%u", mypid);
    }
    else
    {
      printf("%s: Failed to open PID file %s; %s\r\n",
             programName, PIDFILE, strerror(errno));
      return 1;
    }
  }
  else                                /* check for duplicate process. */
  {
    errno = 0;
    if (NULL == fgets(buf, sizeof(buf), pidfile))
    {
      printf("%s: Failed to read PID from file %s; %s\r\n",
             programName, PIDFILE, strerror(errno));
      return 1;
    }

    errno = 0;
    runningpid = strtoul(buf, NULL, 10);
    if (0 != errno)
    {
      printf("%s: Failed to convert PID from file %s; %s\r\n",
             programName, PIDFILE, strerror(errno));
      return 1;
    }

    /* ESRCH means process does not exist.  Any other error value indicates a running
     * process. */
    errno = 0;
    if ((0 == kill(runningpid, 0)) ||
        (ESRCH != errno))
    {
      printf("%s already running with PID %d.  Exiting...\r\n",
             programName, runningpid);
      return 1;
    }

    fseek(pidfile, 0, SEEK_SET);
    fprintf(pidfile, "%u", mypid);
  }
  fclose(pidfile);

  return 0;
}

static char *stemname(char *path)
{
  regex_t regex;
  regmatch_t *matches;
  char *buffer;
  char *stem;
  char regexString[] = "(.*/){0,1}([[:print:]]+)\\..";
  int rc;
  size_t length;
  int numSubs;

  rc = regcomp(&regex, regexString, REG_EXTENDED);
  if (0 != rc)
  {
    length = regerror(rc, &regex, NULL, 0);
    buffer = malloc(length + 1);
    (void)regerror(rc, &regex, buffer, length);
    printf("%s\r\n", buffer);
    regfree(&regex);
    return NULL;
  }

  numSubs = regex.re_nsub + 1;
  matches = calloc(numSubs, sizeof(matches[0]));
  if (NULL == matches)
  {
    printf("Failed to alloc memory for regex match.\r\n");
    regfree(&regex);
    return NULL;
  }

  if (0 != regexec(&regex, path, numSubs, matches, 0))
  {
    printf("Bad file name \"%s\" -- does not match regex.\r\n", path);
    free(matches);
    regfree(&regex);
    return NULL;
  }

  path[matches[2].rm_eo] = '\0';
  stem = &path[matches[2].rm_so];

  free(matches);
  regfree(&regex);

  return stem;
}

static void
ind_ofdpa_event_socket_ready(int socket_id, void *cookie, int read_ready,
                            int write_ready, int error_seen)
{
  struct timeval timeout;

  if (!read_ready)
  {
    AIM_LOG_ERROR("Error: read not ready for connection");
    return;
  }

  if (error_seen)
  {
    AIM_LOG_ERROR("Error seen on socket");
    return;
  }

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  while ((ofdpaEventReceive(&timeout)) == OFDPA_E_NONE)
  {
    ind_ofdpa_flow_event_receive();
    ind_ofdpa_port_event_receive();
  }
  return;
}

static void
ind_ofdpa_pkt_socket_ready(int socket_id, void *cookie, int read_ready,
                            int write_ready, int error_seen)
{
  if (!read_ready)
  {
    AIM_LOG_ERROR("Error: read not ready for connection");
    return;
  }

  if (error_seen)
  {
    AIM_LOG_ERROR("Error seen on socket");
    return;
  }

  ind_ofdpa_pkt_receive();

  return;
}

int main(int argc, char *argv[])
{
  char *programName = basename(strdup(argv[0]));
  char fullProgName[200];
  char exeName[100];
  pid_t mypid = getpid();
  char *fileStemName;
#ifdef OFAGENT_APP
  const char *names;
  int j;
#endif
  int i;
  static char docBuffer[600];
  OFDPA_ERROR_t     rc;

  /* Our argp parser. */
  struct argp argp =
    {
      .doc      = docBuffer,
      .options  = options,
      .parser   = parse_opt,
    };

  arguments_t arguments =
  {
    .agentdebuglvl   = 0,
#ifdef OFAGENT_APP
    .debuglvl   = 0,
    .debugComps = { 0 },
#endif
    .dpid = OFSTATEMANAGER_CONFIG_DPID_DEFAULT,
  };

  fileStemName = stemname(strdup(__FILE__));
  if (NULL == fileStemName)
  {
    exit(EXIT_FAILURE);
  }

  argp_program_version = versionBuf;

  strcpy(docBuffer, "Runs the main OFAgent application.\vDefault values:\n");
  i = strlen(docBuffer);
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "OFAGENTDEBUGLVL  = %d\n", 0);
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "Valid OF Agent debug levels are 0 - %d.\n", 2);
#ifdef OFAGENT_APP
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "OFDPADEBUGLVL  = %d\n", 0);
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "Valid OF-DPA debug levels are 0 - %d.\n", 0);
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "No components enabled for debug:\n");
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "Valid OF-DPA components are:\n");

  for (j = OFDPA_COMPONENT_FIRST; j < OFDPA_COMPONENT_MAX; j++)
  {
    names = ofdpaDebugComponentNameGet(j);
    if (NULL != names)
    {
      i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "\t%2d = %s\n", j, names);
    }
  }
#endif
  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "DATAPATHID = 0x%016llX\n", (long long unsigned int)OFSTATEMANAGER_CONFIG_DPID_DEFAULT);

  i += snprintf(&docBuffer[i], sizeof(docBuffer) - i, "\n");

  AIM_LOG_STRUCT_REGISTER();

  __ind_ofdpa_driver_module_init__();
  __ofstatemanager_module_init__();
  __biglist_module_init__();
  __configuration_module_init__();
  __ofconnectionmanager_module_init__();
  __socketmanager_module_init__();

  /*
   * Display the build date and time.
   */
  AIM_LOG_MSG("\r\n%s\r\n\r\n", versionBuf);

  /* Parse our arguments; every option seen by `parse_opt' will be reflected in
     `arguments'. */
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (is_already_running(programName))
  {
    exit(EXIT_FAILURE);
  }

  sprintf(exeName, "/proc/%u/exe", mypid);
  errno = 0;
  if (-1 == readlink(exeName, fullProgName, sizeof(fullProgName)))
  {
    printf("%s: Failed to determine full path of executable, %s.\r\n",
           __FUNCTION__, strerror(errno));
    exit(EXIT_FAILURE);
  }

  errno = 0;
  if (-1 == chdir(dirname(fullProgName)))
  {
    printf("%s: Failed to set current directory to the executable's directory (%s), %s.\r\n",
           __FUNCTION__, fullProgName, strerror(errno));
    exit(EXIT_FAILURE);
  }

#ifdef OFAGENT_APP

  if (arguments.debuglvl != ofdpaDebugVerbosityGet())
  {
    printf("Setting debug verbosity to %d.\r\n", arguments.debuglvl);
    ofdpaDebugVerbositySet(arguments.debuglvl);
  }

  for (j = OFDPA_COMPONENT_FIRST; j < OFDPA_COMPONENT_MAX; j++)
  {
    if (arguments.debugComps[j])
    {
      printf("Enabling debugging for component %d.\r\n", j);
      ofdpaDebugComponentEnable(j);
    }
  }
#endif
  /* Set up Indigo Agent log level */
  if (arguments.agentdebuglvl >= INDIGO_LOGLEVEL_DEFAULT) {
      aim_log_fid_set_all(AIM_LOG_FLAG_FATAL, 1);
      aim_log_fid_set_all(AIM_LOG_FLAG_ERROR, 1);
      aim_log_fid_set_all(AIM_LOG_FLAG_WARN, 1);
  }

  if (arguments.agentdebuglvl >= INDIGO_LOGLEVEL_VERBOSE) {
      aim_log_fid_set_all(AIM_LOG_FLAG_VERBOSE, 1);
  }

  if (arguments.agentdebuglvl >= INDIGO_LOGLEVEL_TRACE) {
      aim_log_fid_set_all(AIM_LOG_FLAG_TRACE, 1);
  }

  /* Setup Indigo DPID */
  printf("OF Datapath ID: 0x%016llX\n", (long long unsigned int)arguments.dpid);
  (void)indigo_core_dpid_set(arguments.dpid);

  /* Initialize all modules */
  printf("Initializing the system.\r\n");

  rc = ofdpaClientInitialize(programName);
  if (rc != OFDPA_E_NONE)
  {
    return rc;
  }

  if (ind_soc_init(&soc_cfg) < 0) {
      AIM_LOG_FATAL("Failed to initialize Indigo socket manager");
      return 1;
  }

  if (ind_cxn_init(&cxn_cfg) < 0) {
      AIM_LOG_FATAL("Failed to initialize Indigo connection manager");
      return 1;
  }

  if (ind_core_init(&core_cfg) < 0) {
      AIM_LOG_FATAL("Failed to initialize Indigo core module");
      return 1;
  }

  /* Enable all modules */

  if (ind_soc_enable_set(1) < 0) {
      AIM_LOG_FATAL("Failed to enable Indigo socket manager");
      return 1;
  }

  if (ind_cxn_enable_set(1) < 0) {
      AIM_LOG_FATAL("Failed to enable Indigo connection manager");
      return 1;
  }

  if (ind_core_enable_set(1) < 0) {
      AIM_LOG_FATAL("Failed to enable Indigo core module");
      return 1;
  }

  /* Add controllers from command line */
  {
      biglist_t *element;
      char *str;
      BIGLIST_FOREACH_DATA(element, controllers, char *, str) {
          AIM_LOG_MSG("Adding controller %s", str);

          indigo_cxn_protocol_params_t proto;
          if (parse_controller(str, &proto, OF_TCP_PORT) < 0) {
              AIM_LOG_FATAL("Failed to parse controller string '%s'", str);
              return 1;
          }

          indigo_cxn_config_params_t config = {
              .version = ofagent_of_version,
              .cxn_priority = 0,
              .local = 0,
              .listen = 0,
              .periodic_echo_ms = 10000,
              .reset_echo_count = 3,
          };

          indigo_cxn_id_t cxn_id;
          if (indigo_cxn_connection_add(&proto, &config, &cxn_id) < 0) {
              AIM_LOG_FATAL("Failed to add controller %s", str);
              return 1;
          }
      }
  }

  /* Add listening sockets from command line */
  {
      biglist_t *element;
      char *str;
      BIGLIST_FOREACH_DATA(element, listeners, char *, str) {
          AIM_LOG_MSG("Adding listener %s", str);

          indigo_cxn_protocol_params_t proto;
          if (parse_controller(str, &proto, 6634) < 0) {
              AIM_LOG_FATAL("Failed to parse listener string '%s'", str);
              return 1;
          }

          indigo_cxn_config_params_t config = {
              .version = ofagent_of_version,
              .cxn_priority = 10,
              .local = 1,
              .listen = 1,
              .periodic_echo_ms = 0,
              .reset_echo_count = 0,
          };

          indigo_cxn_id_t cxn_id;
          if (indigo_cxn_connection_add(&proto, &config, &cxn_id) < 0) {
              AIM_LOG_FATAL("Failed to add listener %s", str);
              return 1;
          }
      }
  }

  of_desc_str_t mfr_desc = "Broadcom Corp.";
  ind_core_mfr_desc_set(mfr_desc);

  of_desc_str_t sw_desc = "OF-DPA 1.0";
  ind_core_sw_desc_set(sw_desc);

  of_desc_str_t hw_desc = "";
  snprintf(hw_desc, sizeof(hw_desc), "OF-DPA 1.0");
  ind_core_hw_desc_set(hw_desc);

  of_desc_str_t dp_desc = "";
  char hostname[256];
  char domainname[256];
  if (gethostname(hostname, sizeof(hostname))) {
      sprintf(hostname, "(unknown)");
  }
  if (getdomainname(domainname, sizeof(domainname))) {
      sprintf(domainname, "(unknown)");
  }
  snprintf(dp_desc, sizeof(dp_desc), "%s.%s pid %d",
           hostname, domainname, getpid());
  ind_core_dp_desc_set(dp_desc);

  of_serial_num_t serial_num = "";
  ind_core_serial_num_set(serial_num);

  /* The SIGHUP handler triggers sighup_callback to run in the main loop. */
  if ((sighup_eventfd = eventfd(0, 0)) < 0) {
      AIM_LOG_FATAL("Failed to allocate eventfd");
      abort();
  }
  signal(SIGHUP, sighup);
  if (ind_soc_socket_register(sighup_eventfd, sighup_callback, NULL) < 0) {
      abort();
  }

  /* The SIGTERM handler triggers sigterm_callback to run in the main loop. */
  if ((sigterm_eventfd = eventfd(0, 0)) < 0) {
      AIM_LOG_FATAL("Failed to allocate eventfd");
      abort();
  }
  signal(SIGTERM, sigterm);
  if (ind_soc_socket_register(sigterm_eventfd, sigterm_callback, NULL) < 0) {
      abort();
  }

  if (ind_soc_socket_register(ofdpaClientEventSockFdGet(), ind_ofdpa_event_socket_ready, NULL) < 0)
  {
    return 1;
  }

  if (ind_soc_socket_register(ofdpaClientPktSockFdGet(), ind_ofdpa_pkt_socket_ready, NULL) < 0)
  {
    return 1;
  }

  ind_soc_select_and_run(-1);

  AIM_LOG_MSG("Stopping %s", argp_program_version);

  ind_core_finish();
  ind_cxn_finish();
  ind_soc_finish();
  return 0;
}
