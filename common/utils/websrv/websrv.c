/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file common/utils/websrv/websrv.c
 * \brief: implementation of web API
 * \author Francois TABURET
 * \date 2022
 * \version 0.1
 * \company NOKIA BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
 #include <jansson.h>
 #include <ulfius.h>
 #include "common/config/config_userapi.h"
 #include "common/utils/LOG/log.h"
 #include "common/utils/websrv/websrv.h"
 #include "executables/softmodem-common.h"
 #define WEBSERVERCODE
 #include "common/utils/telnetsrv/telnetsrv.h"
 
 
 static websrv_params_t websrvparams;
 paramdef_t websrvoptions[] = {
  /*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
  /*                                            configuration parameters for telnet utility                                                                                      */
  /*   optname                              helpstr                paramflags           XXXptr                               defXXXval               type                 numelt */
  /*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
  {"listenaddr",                   "<listen ip address>\n",         0,                 uptr:&websrvparams.listenaddr,        defstrval:"0.0.0.0",            TYPE_IPV4ADDR,  0 },
  {"listenport",                   "<local port>\n",                0,                 uptr:&(websrvparams.listenport),      defuintval:8090,                TYPE_UINT,      0 },
  {"priority",                     "<scheduling policy (0-99)\n",   0,                 iptr:&websrvparams.priority,          defuintval:0,                   TYPE_INT,       0 },
  {"debug",                        "<debug level>\n",               0,                 uptr:&websrvparams.dbglvl,            defuintval:0,                   TYPE_UINT,      0 },
};
 int websrv_callback_default (const struct _u_request * request, struct _u_response * response, void * user_data) {
  return U_CALLBACK_CONTINUE;
}

int websrv_callback_get_softmodeminfo(const struct _u_request * request, struct _u_response * response, void * user_data) {
  char *cfgfile=CONFIG_GETCONFFILE ;
  char *execfunc=get_softmodem_function(NULL);
  telnetsrv_params_t *telnetparams;
  
  
  telnetparams = get_telnetsrv_params();
  
  json_t * body;
  json_t *status = json_pack("{s[{ss}{ss}]}","display_status", "Config file", cfgfile, "Executable function", execfunc);
  if (status==NULL) {
	  LOG_E(UTIL,"websrv cannot encode status info\n");
  }

  char cmdnames[8192]="?";
  for (int i=0; telnetparams->CmdParsers[i].var != NULL && telnetparams->CmdParsers[i].cmd != NULL; i++) {
 //     client_printf("   module %i = %s:\n",i,telnetparams.CmdParsers[i].module);

 //     for(j=0; telnetparams.CmdParsers[i].var[j].varvalptr != NULL ; j++) {
 //       client_printf("      %s [get set] %s <value>\n",
 //                     telnetparams.CmdParsers[i].module, telnetparams.CmdParsers[i].var[j].varname);
 //     }

 //     for(j=0; telnetparams.CmdParsers[i].cmd[j].cmdfunc != NULL ; j++) {
 //       client_printf("      %s %s %s\n",
 //                     telnetparams.CmdParsers[i].module,telnetparams.CmdParsers[i].cmd[j].cmdname,
 //                     telnetparams.CmdParsers[i].cmd[j].helpstr);
 //     }
    }  
  json_t *cmds = json_pack("{s[s]}","menu_cmds", cmdnames);
  if (cmds==NULL) {
	  LOG_E(UTIL,"websrv cannot encode cmds\n");
  }  
  body=json_pack("{s[oo]}","main_oai softmodem",status,cmds);
  if (body==NULL) {
	  LOG_E(UTIL,"websrv cannot encode body response\n");
  }
  
  int us=ulfius_add_header_to_response(response,"content-type" ,"application/json");
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_add_header_to_response)");
	  LOG_E(UTIL,"websrv cannot set response header type ulfius error %d \n",us);
  }  
  
  us=ulfius_set_json_body_response(response, 200, body);
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_set_json_body_response)");
	  LOG_E(UTIL,"websrv cannot set body response ulfius error %d \n",us);
  }
//  ulfius_set_string_body_response(response, 200, cfgfile);
  return U_CALLBACK_CONTINUE;
}

 void* websrv_autoinit() {
   int ret;
   
  memset(&websrvparams,0,sizeof(websrvparams));
  config_get( websrvoptions,sizeof(websrvoptions)/sizeof(paramdef_t),"websrv");
  // Set the framework port number
  websrvparams.instance = malloc(sizeof(struct _u_instance));
  
  
  
  if (ulfius_init_instance(websrvparams.instance, websrvparams.listenport, NULL, NULL) != U_OK) {
    LOG_W(UTIL, "Error,cannot init websrv\n");
    free(websrvparams.instance);
    return(NULL);
  }
  
  u_map_put(websrvparams.instance->default_headers, "Access-Control-Allow-Origin", "*");
  
  // Maximum body size sent by the client is 1 Kb
  websrvparams.instance->max_post_body_size = 1024;
  
  // Endpoint list declaration
  ulfius_add_endpoint_by_val(websrvparams.instance, "GET", "oaisoftmodem", NULL, 0, &websrv_callback_get_softmodeminfo, NULL);
 //ulfius_add_endpoint_by_val(&instance, "GET", "softmodem", "", 0, &callback_get_empty_response, NULL);
 // ulfius_add_endpoint_by_val(&instance, "GET", PREFIX, "/multiple/:multiple/:multiple/:not_multiple", 0, &callback_all_test_foo, NULL);
 // ulfius_add_endpoint_by_val(&instance, "POST", PREFIX, NULL, 0, &callback_post_test, NULL);
 // ulfius_add_endpoint_by_val(&instance, "GET", PREFIX, "/param/:foo", 0, &callback_all_test_foo, "user data 1");
 // ulfius_add_endpoint_by_val(&instance, "POST", PREFIX, "/param/:foo", 0, &callback_all_test_foo, "user data 2");
 // ulfius_add_endpoint_by_val(&instance, "PUT", PREFIX, "/param/:foo", 0, &callback_all_test_foo, "user data 3");
//  ulfius_add_endpoint_by_val(&instance, "DELETE", PREFIX, "/param/:foo", 0, &callback_all_test_foo, "user data 4");
 // ulfius_add_endpoint_by_val(&instance, "GET", PREFIXCOOKIE, "/:lang/:extra", 0, &callback_get_cookietest, NULL);
  
  // default_endpoint declaration
  ulfius_set_default_endpoint(websrvparams.instance, &websrv_callback_default, NULL);
  
  // Start the framework
//  if (argc == 4 && o_strcmp("-secure", argv[1]) == 0) {
    // If command-line options are -secure <key_file> <cert_file>, then open an https connection
//    char * key_pem = read_file(argv[2]), * cert_pem = read_file(argv[3]);
//    ret = ulfius_start_secure_framework(&instance, key_pem, cert_pem);
 //   o_free(key_pem);
 //   o_free(cert_pem);
 // } else {
    // Open an http connection
    ret = ulfius_start_framework(websrvparams.instance);
 // }
  
  if (ret == U_OK) {
    LOG_I(UTIL, "Web server started on port %d", websrvparams.instance->port);
  } else {
    LOG_W(UTIL,"Error starting web server on port %d\n",websrvparams.instance->port);
  }
 return websrvparams.instance;

}

void websrv_end(void *webinst) {
  ulfius_stop_framework((struct _u_instance *)webinst);
  ulfius_clean_instance((struct _u_instance *)webinst);
  
  return;
}
