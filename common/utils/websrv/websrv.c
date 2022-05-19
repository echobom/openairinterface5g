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
 #include <libgen.h>
 #include <jansson.h>
 #include <ulfius.h>
 #include "common/config/config_userapi.h"
 #include "common/utils/LOG/log.h"
 #include "common/utils/websrv/websrv.h"
 #include "executables/softmodem-common.h"
 #define WEBSERVERCODE
 #include "common/utils/telnetsrv/telnetsrv.h"
 #include <arpa/inet.h>

 
 static websrv_params_t websrvparams;
 static websrv_printf_t websrv_printf_buff;
 paramdef_t websrvoptions[] = {
  /*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
  /*                                            configuration parameters for telnet utility                                                                                      */
  /*   optname                              helpstr                paramflags           XXXptr                               defXXXval               type                 numelt */
  /*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
  {"listenaddr",                   "<listen ip address>\n",         0,                 uptr:&websrvparams.listenaddr,        defstrval:"0.0.0.0",            TYPE_IPV4ADDR,  0 },
  {"listenport",                   "<local port>\n",                0,                 uptr:&(websrvparams.listenport),      defuintval:8090,                TYPE_UINT,      0 },
  {"priority",                     "<scheduling policy (0-99)\n",   0,                 iptr:&websrvparams.priority,          defuintval:0,                   TYPE_INT,       0 },
  {"debug",                        "<debug level>\n",               0,                 uptr:&websrvparams.dbglvl,            defuintval:0,                   TYPE_UINT,      0 },
  {"url",                          "<server url>\n",                0,                 strptr:&websrvparams.url,             defstrval:"index.html",         TYPE_STRING,    0 }, 
  {"cert",                         "<cert file>\n",                 0,                 strptr:&websrvparams.certfile,        defstrval:NULL,                 TYPE_STRING,    0 }, 
  {"key",                          "<key file>\n",                  0,                 strptr:&websrvparams.keyfile,         defstrval:NULL,                 TYPE_STRING,    0 },
};
int websrv_add_endpoint( char **http_method, int num_method, const char * url_prefix,const char * url_format,
                         int (* callback_function[])(const struct _u_request * request, 
                         struct _u_response * response,
                         void * user_data),
                         void * user_data) ;
    
void register_module_endpoints(cmdparser_t *module) ;
                         
void websrv_printjson(char * label, json_t *jsonobj){
	char *jstr = json_dumps(jsonobj,0);
	LOG_I(UTIL,"[websrv] %s:%s\n", label, (jstr==NULL)?"??\n":jstr);
    free(jstr);
}
void websrv_gettbldata_response(struct _u_response * response,webdatadef_t * wdata) ;
int websrv_callback_get_softmodemcmd(const struct _u_request * request, struct _u_response * response, void * user_data);
/*-----------------------------------*/
/* build a json body in a response */
void websrv_jbody( struct _u_response * response, json_t *jbody) {
  websrv_printjson( (char *)__FUNCTION__ , jbody);
  int us=ulfius_add_header_to_response(response,"content-type" ,"application/json");
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_add_header_to_response)");
	  LOG_E(UTIL,"[websrv] cannot set response header type ulfius error %d \n",us);
  }   
  us=ulfius_set_json_body_response(response, 200, jbody);
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_set_json_body_response)");
	  LOG_E(UTIL,"[websrv] cannot set body response ulfius error %d \n",us);
  }
  return;
}

/*----------------------------------------------------------------*/
/* format a json string array in a response body                  */
int websrv_string_response(char *astring, struct _u_response * response, int httpstatus) {
  json_t *jstr = json_array(); 
  char *tokctx;
  char *aline=strtok_r(astring,"\n",&tokctx);
  while(aline != NULL) {
    json_t *jline=json_string(aline);
    json_array_append_new(jstr,jline);
    aline=strtok_r(NULL,"\n",&tokctx);
  }
  json_t *jbody = json_pack("{s:o}","display",jstr);
  websrv_jbody(response,jbody);
  return 0; 
}

/* */
void websrv_printf_start(struct _u_response * response, int buffsize ) {
  pthread_mutex_lock(&(websrv_printf_buff.mutex));	
  websrv_printf_buff.buff = malloc(buffsize);
  websrv_printf_buff.buffptr = websrv_printf_buff.buff;
  websrv_printf_buff.buffsize = buffsize;
  websrv_printf_buff.response = response;
}

void websrv_printf_atpos( int pos, const char *message,  ...) {
  va_list va_args;
  va_start(va_args, message);
  
  websrv_printf_buff.buffptr = websrv_printf_buff.buff + pos + vsnprintf( websrv_printf_buff.buff + pos, websrv_printf_buff.buffsize - pos -1,message, va_args );

  va_end(va_args);
  return ;
}

void websrv_printf( const char *message,  ...) {
  va_list va_args;
  va_start(va_args, message);
  websrv_printf_buff.buffptr +=  vsnprintf( websrv_printf_buff.buffptr,
                                            websrv_printf_buff.buffsize - (websrv_printf_buff.buffptr - websrv_printf_buff.buff) - 1,message, va_args );	
  
  va_end(va_args); 
  return ;
}

void websrv_printf_end(int httpstatus ) {
  if (httpstatus >= 200 && httpstatus < 300) {
    LOG_I(UTIL,"[websrv] %s\n",websrv_printf_buff.buff);
    websrv_string_response(websrv_printf_buff.buff, websrv_printf_buff.response, httpstatus) ;   
  } else {
    LOG_W(UTIL,"[websrv] %s\n",websrv_printf_buff.buff);
    ulfius_set_binary_body_response(websrv_printf_buff.response,httpstatus , websrv_printf_buff.buff,websrv_printf_buff.buffptr - websrv_printf_buff.buff);
  }
  
  free(websrv_printf_buff.buff);
  websrv_printf_buff.buff=NULL;
  pthread_mutex_unlock(&(websrv_printf_buff.mutex));

}

/* buid a response via a webdatadef_t structure containing one string column               */
void websrv_printf_tbl_end(int httpstatus ) {
  webdatadef_t pdata;
  char *tokctx;
  
  pdata.numcols=1;
  pdata.numlines=0;
  pdata.columns[0].coltype = TELNET_VARTYPE_STRING | TELNET_CHECKVAL_RDONLY;
  strcpy(pdata.columns[0].coltitle," ");
  for ( char *alineptr=strtok_r(websrv_printf_buff.buff,"\n",&tokctx); alineptr != NULL ; alineptr=strtok_r(NULL,"\n",&tokctx) ) {
	  pdata.lines[pdata.numlines].val[0]=alineptr;
	  pdata.numlines++;  
  }
  websrv_gettbldata_response(websrv_printf_buff.response,&pdata);
  free(websrv_printf_buff.buff);
  websrv_printf_buff.buff=NULL;
  pthread_mutex_unlock(&(websrv_printf_buff.mutex));

}


/*--------------------------------------------------------------------------------------------------*/
/* format a json response from a result table returned from a call to a telnet server command       */
void websrv_gettbldata_response(struct _u_response * response,webdatadef_t * wdata) {
	json_t *jcols = json_array();
    json_t *jdata = json_array();
    char *coltype;
    for (int i=0; i<wdata->numcols; i++) {
      if(wdata->columns[i].coltype & TELNET_CHECKVAL_BOOL)
        coltype="boolean";
      else if (wdata->columns[i].coltype & TELNET_VARTYPE_STRING)
        coltype="string";
      else
        coltype="number";
      json_t *acol=json_pack("{s:s,s:s,s:b}","name",wdata->columns[i].coltitle,"type",coltype,
                            "modifiable",( wdata->columns[i].coltype & TELNET_CHECKVAL_RDONLY)?0:1  );
      json_array_append_new(jcols,acol);                   
      }    
	for (int i=0; i<wdata->numlines ; i++) {
        json_t *jval; 
        json_t *jline=json_array();
        for (int j=0; j<wdata->numcols; j++) {  
          if(wdata->columns[j].coltype & TELNET_CHECKVAL_BOOL)
            jval=json_string(wdata->lines[i].val[j]);
          else if (wdata->columns[j].coltype & TELNET_VARTYPE_STRING)
            jval=json_string(wdata->lines[i].val[j]);
//          else if (wdata->columns[j].coltype == TELNET_VARTYPE_DOUBLE)
//            jval=json_real((double)(wdata->lines[i].val[j]));
          else
            jval=json_integer((long)(wdata->lines[i].val[j]));
          json_array_append_new(jline,jval);                   
        }
      json_array_append_new(jdata,jline);  
    }
    json_t *jbody=json_pack("{s:[],s:{s:o,s:o}}","display","table","columns",jcols,"rows",jdata);
    websrv_jbody(response,jbody);
    telnetsrv_freetbldata(wdata);
}

/*----------------------------------------------------------------------------------------------------------*/
/* callbacks and utility functions to stream a file */
char * websrv_read_file(const char * filename) {
  char * buffer = NULL;
  long length;
  FILE * f = fopen (filename, "rb");
  if (f != NULL) {
    fseek (f, 0, SEEK_END);
    length = ftell (f);
    fseek (f, 0, SEEK_SET);
    buffer = malloc (length + 1);
    if (buffer != NULL) {
      int rlen = fread (buffer, 1, length, f);
      if (rlen !=length) {
        free(buffer);
        LOG_E(UTIL,"[websrv] couldn't read %s_\n",filename);
        return NULL;
      }
      buffer[length] = '\0';
    }
    fclose (f);
  }
  return buffer;
}
/* callbacks to send static streams */
static ssize_t callback_stream(void * cls, uint64_t pos, char * buf, size_t max) {
  if (cls != NULL) {
    return fread (buf, sizeof(char), max, (FILE *)cls);
  } else {
    return U_STREAM_END;
  }
}

static void callback_stream_free(void * cls) {
  if (cls != NULL) {
    fclose((FILE *)cls);
  }
}

FILE *websrv_getfile(char *filename, struct _u_response * response) {
    FILE *f = fopen (filename, "rb");
  int length;
  
  if (f) {
    fseek (f, 0, SEEK_END);
    length = ftell (f);
    fseek (f, 0, SEEK_SET);
    LOG_I(UTIL,"[websrv] sending %d bytes from %s\n", length, filename);
  } else {
    LOG_E(UTIL,"[websrv] couldn't open %s\n",filename);
    return NULL;
  }

  char *content_type="text/html";
  size_t nl = strlen(filename);
  if ( nl >= 3 && !strcmp(filename + nl - 3, "css"))
     content_type="text/css";

  int ust=ulfius_add_header_to_response(response,"content-type" ,content_type);
  if (ust != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_add_header_to_response)");
	  LOG_E(UTIL,"[websrv] cannot set response header type ulfius error %d \n",ust);
      fclose(f);
      return NULL;
  }
  
  ust=ulfius_set_stream_response(response, 200, callback_stream, callback_stream_free, length, 1024, f);
  if(ust != U_OK) {
    LOG_E(UTIL,"[websrv] ulfius_set_stream_response error %d\n",ust);
    fclose(f);
    return NULL;
  }
  return f;  
    
}
/*------------------------------------------------------------------------------------------------------------------------*/
int websrv_callback_set_moduleparams(const struct _u_request *request, struct _u_response *response, void *user_data) {
  websrv_printf_start(response,200);
  LOG_I(UTIL,"[websrv] callback_module_set received: %s %s\n",request->http_verb,request->http_url);
	 json_error_t jserr;
	 json_t* jsbody = ulfius_get_json_body_request (request, &jserr);
     int httpstatus=404;
	 if (jsbody == NULL) {
	   websrv_printf("cannot find json body in %s %s\n",request->http_url, jserr.text );
       httpstatus=400;	 
	 } else {
	   websrv_printjson( (char *)__FUNCTION__,jsbody);
       if (user_data == NULL) {
         httpstatus=500;
         websrv_printf("%s: NULL user data",request->http_url);
       } else {
	     cmdparser_t * modulestruct = (cmdparser_t *)user_data;
	     int   rawval;
	     char *cmdName;
	     json_t *parray=json_array();
	     json_error_t jerror;
         int ures =  json_unpack_ex(jsbody, &jerror,0,"{s:i,s:s,s:o}", "rawIndex",&rawval,"cmdName", &cmdName,"params",&parray);
         if (ures != 0) {
			 websrv_printf("cannot unpack json body from %s: %s \n",request->http_url,jerror.text);
		 } else {
			 int psize=json_array_size(parray);
			 webdatadef_t datatbl;
			 datatbl.numlines=rawval;
			 datatbl.numcols=psize;			 
			 LOG_I(UTIL,"[websrv] rawIndex=%i, cmdName=%s, params=array of %i table values\n", rawval, cmdName,psize);
			 for (int i=0 ; i<psize; i++) {
				 json_t *jelem = json_array_get(parray,i);
				 char *cvalue;
				 char *cname;
				 char *ctype;
				 int cmod;
				 ures =  json_unpack_ex(jelem, &jerror,0,"{s:s,s:{s:s,s:s,s:b}}", "value",&cvalue,"col", 
				                                          "name",&cname,"type",&ctype,"modifiable",&cmod);
                 if (ures != 0) {
			       websrv_printf("cannot unpack json element %i %s\n",i,jerror.text);
		         } else {
					LOG_I(UTIL,"[websrv] element%i, value=%s, name %s type %s\n", i, cvalue, cname, ctype); 
			        snprintf(datatbl.columns[i].coltitle,TELNET_CMD_MAXSIZE-1,"%s",cname);
			        datatbl.columns[i].coltype=TELNET_VARTYPE_STRING;
			        datatbl.lines[0].val[i]=cvalue;	        
				 } // json_unpack_ex(jelem OK
			 } // for i
		     for ( telnetshell_cmddef_t *cmd = modulestruct->cmd ; cmd->cmdfunc != NULL ;cmd++) {
               if ( strcmp(cmd->cmdname,cmdName) == 0 && (( cmd->cmdflags & TELNETSRV_CMDFLAG_TELNETONLY) == 0) ){
				 httpstatus=200;
				 if(strncmp(cmdName,"show",4) == 0) {
				   sprintf(cmdName,"%s","set");
				   cmdName[3]=' ';
				   }
			     cmd->webfunc_getdata(cmdName,websrvparams.dbglvl,&datatbl,websrv_printf);
                 break;
               }
            }//for	*cmd	
		 } // json_unpack_ex(jsbody OK
       }//user_data
     } //sbody
  websrv_printf_end(httpstatus);
  return U_CALLBACK_COMPLETE;      	
}
/*------------------------------------------------------------------------------------------------------------------------*/
/* callback processing main ((initial) url (<address>/<websrvparams.url> */
int websrv_callback_get_mainurl(const struct _u_request * request, struct _u_response * response, void * user_data) {
  LOG_I(UTIL,"[websrv] Requested file is: %s\n",request->http_url);  

  FILE *f = websrv_getfile(websrvparams.url,response) ;
  if (f == NULL)
    return U_CALLBACK_ERROR;
  return U_CALLBACK_CONTINUE;
}


/* default callback tries to find a file in the web server repo (path exctracted from <websrvparams.url>) and if found streams it */
 int websrv_callback_default (const struct _u_request * request, struct _u_response * response, void * user_data) {
 LOG_I(UTIL,"[websrv] Requested file is: %s %s\n",request->http_verb,request->http_url);
  if (request->map_post_body != NULL)
    for (int i=0; i<u_map_count(request->map_post_body) ; i++)
      LOG_I(UTIL,"[websrv] POST parameter %i %s : %s\n",i,u_map_enum_keys(request->map_post_body)[i], u_map_enum_values(request->map_post_body)[i]	);
  char *tmpurl = strdup(websrvparams.url);
  char *srvdir = dirname(tmpurl);
  if (srvdir==NULL) {
      LOG_E(UTIL,"[websrv] Cannot extract dir name from %s requested file is %s\n",websrvparams.url,request->http_url); 
      return U_CALLBACK_ERROR;
  }
  char *fpath = malloc( strlen(request->http_url)+strlen(srvdir)+2);
  sprintf(fpath,"%s/%s",srvdir, request->http_url);
  FILE *f = websrv_getfile(fpath,response) ;
  free(fpath);
  free(tmpurl);
  if (f == NULL)
    return U_CALLBACK_ERROR;
  return U_CALLBACK_CONTINUE;
}
/* callback processing  url (<address>/oaisoftmodem/:module/variables or <address>/oaisoftmodem/:module/commands) */
int websrv_callback_newmodule(const struct _u_request * request, struct _u_response * response, void * user_data) {
	LOG_I(UTIL,"[websrv] callback_newmodule received %s %s\n",request->http_verb,request->http_url);
	if (user_data == NULL) {
	  ulfius_set_string_body_response(response, 500, "Cannot access oai softmodem data");
	  LOG_E(UTIL,"[websrv] callback_newmodule: user-data is NULL");
	  return U_CALLBACK_COMPLETE;	
    }
	telnetsrv_params_t *telnetparams =  (telnetsrv_params_t *)user_data;
    for (int i=0; i<u_map_count(request->map_url) ; i++) {
       LOG_I(UTIL,"[websrv]   url element %i %s : %s\n",i,u_map_enum_keys(request->map_url)[i], u_map_enum_values(request->map_url)[i]	);
       if ( strcmp(u_map_enum_keys(request->map_url)[i],"module") == 0) {
		  for (int j=0; telnetparams->CmdParsers[j].cmd != NULL; j++) {
		/* found the module in the telnet server module array, it was likely not registered at init time */
			 if (strcmp(telnetparams->CmdParsers[j].module, u_map_enum_values(request->map_url)[i]) == 0) {
	           register_module_endpoints( &(telnetparams->CmdParsers[j]) );
	           websrv_callback_get_softmodemcmd(request, response, &(telnetparams->CmdParsers[j]));
	           return U_CALLBACK_CONTINUE;
		     }
          } /* for j */
	   }
    } /* for i */
    ulfius_set_string_body_response(response, 500, "Request for an unknown module");
	return U_CALLBACK_COMPLETE; 
}

/* callback processing  url (<address>/oaisoftmodem/module/variables or <address>/oaisoftmodem/module/commands), options method */
int websrv_callback_okset_softmodem_cmdvar(const struct _u_request * request, struct _u_response * response, void * user_data) {
	 LOG_I(UTIL,"[websrv] : callback_okset_softmodem_cmdvar received %s %s\n",request->http_verb,request->http_url);
     for (int i=0; i<u_map_count(request->map_header) ; i++)
       LOG_I(UTIL,"[websrv] header variable %i %s : %s\n",i,u_map_enum_keys(request->map_header)[i], u_map_enum_values(request->map_header)[i]	);
     int us=ulfius_add_header_to_response(response,"Access-Control-Request-Method" ,"POST");
      if (us != U_OK){
	    ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_add_header_to_response)");
	    LOG_E(UTIL,"[websrv] cannot set response header type ulfius error %d \n",us);
      }  
      us=ulfius_add_header_to_response(response,"Access-Control-Allow-Headers", "content-type"); 
      us=ulfius_set_empty_body_response(response, 200);
      if (us != U_OK){
	    ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_set_empty_body_response)");
	    LOG_E(UTIL,"[websrv] cannot set empty body response ulfius error %d \n",us);
      }  
	 return U_CALLBACK_COMPLETE;   
}
int websrv_callback_set_softmodemvar(const struct _u_request * request, struct _u_response * response, void * user_data) {
	 LOG_I(UTIL,"[websrv] : callback_set_softmodemvar received %s %s\n",request->http_verb,request->http_url);
	 websrv_printf_start(response,256);
	 json_error_t jserr;
	 json_t* jsbody = ulfius_get_json_body_request (request, &jserr);
     int httpstatus=404;
	 if (jsbody == NULL) {
	   websrv_printf("cannot find json body in %s %s\n",request->http_url, jserr.text );
       httpstatus=400;	 
	 } else {
	   websrv_printjson("callback_set_softmodemvar: ",jsbody);
       if (user_data == NULL) {
         httpstatus=500;
         websrv_printf("%s: NULL user data",request->http_url);
       } else {
	     cmdparser_t * modulestruct = (cmdparser_t *)user_data;
         json_t *J=json_object_get(jsbody, "name");
         const char *vname=json_string_value(J);
         for ( telnetshell_vardef_t *var = modulestruct->var; var->varvalptr!= NULL ;var++) {
           if (strncmp(var->varname,vname,TELNET_CMD_MAXSIZE) == 0){
             J=json_object_get(jsbody, "value");
             if(J!=NULL) {
               if (json_is_string(J)) {
				 const char *vval=json_string_value(J);
				 websrv_printf("var %s set to ",var->varname); 
                 int st=telnet_setvarvalue(var,(char *)vval, websrv_printf );
                 if (st>=0) {				   
				   httpstatus=200;
				 } else {
				   httpstatus=500;				 
				 }
			  } else if (json_is_integer(J)) {
				 json_int_t i = json_integer_value(J);
				 switch(var->vartype) {
					 case TELNET_VARTYPE_INT64:
					   *(int64_t *)var->varvalptr=(int64_t)i;
					   break;					 
					 case TELNET_VARTYPE_INT32:
					   *(int32_t *)var->varvalptr=(int32_t)i;
					   break;
					 case TELNET_VARTYPE_INT16:
					   *(int16_t *)var->varvalptr=(int16_t)i;
					   break;	
					 case TELNET_VARTYPE_INT8:
					   *(int8_t *)var->varvalptr=(int8_t)i;
					   break;
					 case TELNET_VARTYPE_UINT:
					   *(unsigned int *)var->varvalptr=(unsigned int)i;
					   break;					   						   				   
					 default:
                       httpstatus=500;
                       websrv_printf(" Cannot set var %s, integer type mismatch\n",vname );
                       break;				 
			     }
			   } else if (json_is_real(J)) {
				 double lf = json_real_value(J); 
				 if(var->vartype==TELNET_VARTYPE_DOUBLE) {
					*(double *)var->varvalptr = lf;
                    httpstatus=200;
                    websrv_printf(" Var %s, set to %g\n",vname, *(double *)var->varvalptr );				 					
				 } else {
                   httpstatus=500;
                   websrv_printf(" Cannot set var %s, real type mismatch\n",vname );					 
				 }
			   }
             } else {
               httpstatus=500;
               websrv_printf("Cannot set var %s, json object is NULL\n",vname );
             }
             break;
           }
         }//for
       }//user_data
     } //sbody
     websrv_printf_end(httpstatus);
	 return U_CALLBACK_COMPLETE;   
}
/* callback processing module url (<address>/oaisoftmodem/module/commands), post method */
int websrv_processwebfunc(struct _u_response * response, cmdparser_t * modulestruct ,telnetshell_cmddef_t *cmd, json_t *jparams) {
  LOG_I(UTIL,"[websrv] : executing command %s %s\n",modulestruct->module,cmd->cmdname);
  
  if (cmd->cmdflags & TELNETSRV_CMDFLAG_GETWEBTBLDATA) {
	webdatadef_t wdata;
    memset(&wdata,0,sizeof(wdata));
    if (cmd->cmdflags & TELNETSRV_CMDFLAG_NEEDPARAM) {
	  if ( jparams == NULL) {
		LOG_W(UTIL,"No parameters sent by frontend for %s %s\n",modulestruct->module,cmd->cmdname);
	  } else {
		int b;
		char *pname, *pvalue, *ptype;
		json_unpack(jparams,"%s:%s,%s:%s,%s:%s,%s,%b","name",&pname,"value",&pvalue,"type",&ptype,"modifiable",&b);
		wdata.numlines=strtol(pvalue,NULL,0);
	  }
	}
	cmd->webfunc_getdata(cmd->cmdname,websrvparams.dbglvl,(webdatadef_t *)&wdata,NULL);	  
	websrv_gettbldata_response(response,&wdata);
  } else {
    websrv_printf_start(response,16384);
    char *sptr=index(cmd->cmdname,' ');
    if (cmd->qptr != NULL) 
      telnet_pushcmd(cmd, (sptr==NULL)?cmd->cmdname:sptr, websrv_printf);
    else
      cmd->cmdfunc((sptr==NULL)?cmd->cmdname:sptr,websrvparams.dbglvl,websrv_printf);
    if (cmd->cmdflags & TELNETSRV_CMDFLAG_PRINTWEBTBLDATA)
      websrv_printf_tbl_end(200);
    else
      websrv_printf_end(200);
  }
  return 200;
}

int websrv_callback_exec_softmodemcmd(const struct _u_request * request, struct _u_response * response, void * user_data) {
	 LOG_I(UTIL,"[websrv] : callback_exec_softmodemcmd received %s %s\n",request->http_verb,request->http_url);
	 if (user_data == NULL) {
	   ulfius_set_string_body_response(response, 500, "Cannot access oai softmodem data");
	   LOG_E(UTIL,"[websrv] callback_exec_softmodemcmd: user-data is NULL");
	   return U_CALLBACK_COMPLETE;	
     } 
	 cmdparser_t * modulestruct = (cmdparser_t *)user_data;
	 json_t* jsbody = ulfius_get_json_body_request (request, NULL);
     int httpstatus=404;
     char *msg="";
	 if (jsbody == NULL) {
	   msg="Unproperly formatted request body";
       httpstatus=400;	 
	 } else {
	   websrv_printjson("callback_exec_softmodemcmd: ",jsbody);
       json_t *J=json_object_get(jsbody, "name");
       const char *vname = json_string_value(J);
       if (vname == NULL ) {
		 msg="No command name in request body";
		 LOG_E(UTIL,"[websrv] command name not found in body\n");
         httpstatus=400;
       } else {
		 httpstatus=501;
		 msg="Unknown command in request body";
         for ( telnetshell_cmddef_t *cmd = modulestruct->cmd ; cmd->cmdfunc != NULL ;cmd++) {
           if ( strcmp(cmd->cmdname,vname) == 0 && (( cmd->cmdflags & TELNETSRV_CMDFLAG_TELNETONLY) == 0) ){
			 J=json_object_get(jsbody, "param");
			 httpstatus=websrv_processwebfunc(response,modulestruct,cmd,J);
             break;
           }
         }//for
	   }
     } //sbody
     if (httpstatus >= 300)
       ulfius_set_string_body_response(response, httpstatus, msg);
	 return U_CALLBACK_COMPLETE;   
}
/* callback processing module url (<address>/oaisoftmodem/module/variables), get method*/
int websrv_callback_get_softmodemvar(const struct _u_request * request, struct _u_response * response, void * user_data) {
		
    LOG_I(UTIL,"[websrv] : callback_get_softmodemvar received %s %s  module %s\n",request->http_verb,request->http_url,
          (user_data==NULL)?"NULL":((cmdparser_t *)user_data)->module);
    if (user_data == NULL) {		
	    ulfius_set_string_body_response(response, 500, "No variables defined for this module");
	    return U_CALLBACK_COMPLETE; 
	}
    cmdparser_t * modulestruct = (cmdparser_t *)user_data;
	LOG_I(UTIL,"[websrv] received  %s variables request\n", modulestruct->module);
	json_t *modulevars = json_array();

     for(int j=0; modulestruct->var[j].varvalptr != NULL ; j++) {
	   char*strval=telnet_getvarvalue(modulestruct->var, j);
	   int modifiable=1;
	   if (modulestruct->var[j].checkval & TELNET_CHECKVAL_RDONLY)
	     modifiable=0;
       json_t *oneaction;
	   switch(modulestruct->var[j].vartype) {
		   case  TELNET_VARTYPE_DOUBLE:
		      oneaction =json_pack("{s:s,s:s,s:g,s:b}","type","number","name",modulestruct->var[j].varname,"value",*(double *)(modulestruct->var[j].varvalptr),"modifiable",modifiable);		   		   
		   case  TELNET_VARTYPE_INT32:
		   case  TELNET_VARTYPE_INT16:
		   case  TELNET_VARTYPE_INT8:	   
		   case  TELNET_VARTYPE_UINT:
		      oneaction =json_pack("{s:s,s:s,s:i,s:b}","type","number","name",modulestruct->var[j].varname,"value",(int)(*(int *)(modulestruct->var[j].varvalptr)),"modifiable",modifiable);
		   break;
		   case  TELNET_VARTYPE_INT64:   
              oneaction =json_pack("{s:s,s:s,s:lli,s:b}","type","number","name",modulestruct->var[j].varname,"value",(int64_t)(*(int64_t *)(modulestruct->var[j].varvalptr)),"modifiable",modifiable);
		   break;
		   case TELNET_VARTYPE_STRING:
		      oneaction =json_pack("{s:s,s:s,s:s,s:b}","type","string","name",modulestruct->var[j].varname,"value",strval,"modifiable",modifiable);
		   break;
		   default:
		      oneaction =json_pack("{s:s,s:s,s:s,s:b}","type","???","name",modulestruct->var[j].varname,"value","???","modifiable",modifiable);
		   break;
	   }
       if (oneaction==NULL) {
	     LOG_E(UTIL,"[websrv] cannot encode oneaction %s/%s\n",modulestruct->module,modulestruct->var[j].varname);
       } else {
	     websrv_printjson("oneaction",oneaction);
       }   
	   free(strval);
       json_array_append(modulevars , oneaction);
     }
     if (json_array_size(modulevars) == 0) {
	   LOG_I(UTIL,"[websrv] no defined variables for %s\n",modulestruct->module);
     } else {
	   websrv_printjson("modulevars",modulevars);
     }

     int us=ulfius_add_header_to_response(response,"content-type" ,"application/json");
     if (us != U_OK){
	   ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_add_header_to_response)");
	   LOG_E(UTIL,"[websrv] cannot set response header type ulfius error %d \n",us);
     }   
     us=ulfius_set_json_body_response(response, 200, modulevars);
     if (us != U_OK){
	   ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_set_json_body_response)");
	   LOG_E(UTIL,"[websrv] cannot set body response ulfius error %d \n",us);
     }   
    return U_CALLBACK_COMPLETE;     
}

/* callback processing module url (<address>/oaisoftmodem/module/commands)*/
int websrv_callback_get_softmodemcmd(const struct _u_request * request, struct _u_response * response, void * user_data) {
    LOG_I(UTIL,"[websrv] : callback_get_softmodemcmd received %s %s  module %s\n",request->http_verb,request->http_url,
          (user_data==NULL)?"NULL":((cmdparser_t *)user_data)->module);
    if (user_data == NULL) {		
	    ulfius_set_string_body_response(response, 500, "No commands defined for this module");
	    return U_CALLBACK_COMPLETE; 
	}
	cmdparser_t *modulestruct = (cmdparser_t *)user_data;
	
	LOG_I(UTIL,"[websrv] received  %s commands request\n", modulestruct->module);
	    json_t *modulesubcom = json_array();
        for(int j=0; modulestruct->cmd[j].cmdfunc != NULL ; j++) {
		  if(strcasecmp("help",modulestruct->cmd[j].cmdname) == 0 || ( modulestruct->cmd[j].cmdflags & TELNETSRV_CMDFLAG_TELNETONLY ) ) {
			continue;
		  }
		json_t *acmd;
		if (modulestruct->cmd[j].cmdflags &  TELNETSRV_CMDFLAG_CONFEXEC) {
		  char confstr[256];
		  snprintf(confstr,sizeof(confstr),"Execute %s ?",modulestruct->cmd[j].cmdname);
		  acmd =json_pack( "{s:s,s:s}", "name",modulestruct->cmd[j].cmdname,"confirm", confstr);
		} else if (modulestruct->cmd[j].cmdflags &  TELNETSRV_CMDFLAG_NEEDPARAM) {
			if (modulestruct->cmd[j].webfunc_getdata != NULL) {
				webdatadef_t wdata;
				modulestruct->cmd[j].webfunc_getdata(modulestruct->cmd[j].cmdname,websrvparams.dbglvl, &(wdata),NULL);
				acmd =json_pack( "{s:s,s:{s:s,s:s,s:s}}", "name",modulestruct->cmd[j].cmdname,"question","display",wdata.tblname ,"pname","P0","type","string");
			}
		}else {
		  acmd =json_pack( "{s:s}", "name",modulestruct->cmd[j].cmdname);
	    }
		json_array_append(modulesubcom , acmd);
        }
        if (modulesubcom==NULL) {
	      LOG_E(UTIL,"[websrv] cannot encode modulesubcom response for %s\n",modulestruct->module);
        } else {
	      websrv_printjson("modulesubcom",modulesubcom);
        }             
        int us=ulfius_add_header_to_response(response,"content-type" ,"application/json");
        if (us != U_OK){
	      ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_add_header_to_response)");
	      LOG_E(UTIL,"[websrv] cannot set response header type ulfius error %d \n",us);
        }   
        us=ulfius_set_json_body_response(response, 200, modulesubcom);
        if (us != U_OK){
	      ulfius_set_string_body_response(response, 500, "Internal server error (ulfius_set_json_body_response)");
	      LOG_E(UTIL,"[websrv] cannot set body response ulfius error %d \n",us);
        }        
	return U_CALLBACK_COMPLETE;
}

int websrv_callback_get_softmodemmodules(const struct _u_request * request, struct _u_response * response, void * user_data) {
  telnetsrv_params_t *telnetparams= get_telnetsrv_params();

  json_t *cmdnames = json_array();
  for (int i=0; telnetparams->CmdParsers[i].var != NULL && telnetparams->CmdParsers[i].cmd != NULL; i++) {
	  json_t *acmd =json_pack( "{s:s}", "name",telnetparams->CmdParsers[i].module);
	  json_array_append(cmdnames, acmd);
    }

  
  int us=ulfius_add_header_to_response(response,"content-type" ,"application/json");
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_add_header_to_response)");
	  LOG_E(UTIL,"[websrv] cannot set modules response header type ulfius error %d \n",us);
  }  
  
  us=ulfius_set_json_body_response(response, 200, cmdnames);
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_set_json_body_response)");
	  LOG_E(UTIL,"[websrv] cannot set modules body response ulfius error %d \n",us);
  } else {
	  websrv_printjson("cmdnames",cmdnames);
  }  
//  ulfius_set_string_body_response(response, 200, cfgfile);
  return U_CALLBACK_COMPLETE;
}
/*---------------------------------------------------------------------------*/
/* callback processing initial url (<address>/oaisoftmodem)*/

/* get info on this modem */
void websrv_add_modeminfo(json_t *modemvars, char *infoname, char *infoval, char *strtype) {
  json_t *info;
  if(strcmp(strtype,"link")==0) {
    char linkstr[255];
    snprintf(linkstr,sizeof(linkstr),"<link href=\"%s\">",infoval);
    info=json_pack("{s:s,s:s,s:s,s:b}","name",linkstr, "value",infoval, "type",strtype,"modifiable",0);
  } else {
    info=json_pack("{s:s,s:s,s:s,s:b}","name",infoname, "value",infoval, "type",strtype,"modifiable",0);
  }
  if (info==NULL) {
	  LOG_E(UTIL,"[websrv] cannot encode modem info %s response\n",infoname);
  } else {
	  websrv_printjson("status body1",info);
  }  
  json_array_append_new(modemvars , info);
} 

int websrv_callback_get_softmodemstatus(const struct _u_request * request, struct _u_response * response, void * user_data) {
  char ipstr[INET_ADDRSTRLEN];
  
  inet_ntop(AF_INET, &(((struct sockaddr_in *)request->client_address)->sin_addr), ipstr, INET_ADDRSTRLEN);
  json_t *modemvars = json_array();
  websrv_add_modeminfo(modemvars,"connected to",ipstr,"string");
  websrv_add_modeminfo(modemvars,"exec function",get_softmodem_function(NULL),"string");
  websrv_add_modeminfo(modemvars,"config_file",CONFIG_GETCONFFILE,"link");
  websrv_add_modeminfo(modemvars,"exec name",config_get_if()->argv[0],"string");
  websrv_add_modeminfo(modemvars,"config debug mode",(config_get_if()->status != NULL)?"yes":"no","string");
  
  int us=ulfius_add_header_to_response(response,"content-type" ,"application/json");
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_add_header_to_response)");
	  LOG_E(UTIL,"[websrv] cannot set status response header type ulfius error %d \n",us);
  }  
  
  us=ulfius_set_json_body_response(response, 200, modemvars);
  if (us != U_OK){
	  ulfius_set_string_body_response(response, 501, "Internal server error (ulfius_set_json_body_response)");
	  LOG_E(UTIL,"[websrv] cannot set status body response ulfius error %d \n",us);
  }
//  ulfius_set_string_body_response(response, 200, cfgfile);
  return U_CALLBACK_COMPLETE;
}


int websrv_add_endpoint( char **http_method, int num_method, const char * url_prefix,const char * url_format,
                         int (* callback_function[])(const struct _u_request * request, 
                                                   struct _u_response * response,
                                                   void * user_data),
                         void * user_data) {
  int status;
  int j=0;
  int priority = (user_data == NULL)?10:0;
  for (int i=0; i<num_method; i++) {
    status=ulfius_add_endpoint_by_val(&(websrvparams.instance),http_method[i],url_prefix,url_format,priority,callback_function[i],user_data);
    if (status != U_OK) {
      LOG_E(UTIL,"[websrv] cannot add endpoint %s %s/%s\n",http_method[i],url_prefix,url_format);
    } else {
      j++;
      LOG_I(UTIL,"[websrv] endpoint %s %s/%s added\n",http_method[i],url_prefix,url_format);
    }
  }
  return j;
}
/* add endpoints for a module, as defined in cmdparser_t telnet server structure */
void register_module_endpoints(cmdparser_t *module) {
  int (* callback_functions_var[3])(const struct _u_request * request, 
                                    struct _u_response * response,
                                    void * user_data) ={websrv_callback_okset_softmodem_cmdvar,websrv_callback_set_softmodemvar,websrv_callback_get_softmodemvar};
  char *http_methods[3]={"OPTIONS","POST","GET"};


  int (* callback_functions_cmd[3])(const struct _u_request * request, 
                                    struct _u_response * response,
                                    void * user_data) ={websrv_callback_okset_softmodem_cmdvar,websrv_callback_exec_softmodemcmd, websrv_callback_get_softmodemcmd};
  char prefixurl[TELNET_CMD_MAXSIZE+20];
  snprintf(prefixurl,TELNET_CMD_MAXSIZE+19,"oaisoftmodem/%s",module->module);
  LOG_I(UTIL,"[websrv] add endpoints %s/[variables or commands] \n",prefixurl);

  websrv_add_endpoint(http_methods,3,prefixurl,"commands" ,callback_functions_cmd, module);
  websrv_add_endpoint(http_methods,3,prefixurl,"variables" ,callback_functions_var, module);
  
  callback_functions_cmd[0]=websrv_callback_okset_softmodem_cmdvar;
  callback_functions_cmd[1]=websrv_callback_set_moduleparams;
  websrv_add_endpoint(http_methods,2,prefixurl,"set" ,callback_functions_cmd,module);  
}


void* websrv_autoinit() {
  int ret;
  telnetsrv_params_t *telnetparams= get_telnetsrv_params(); 
  memset(&websrvparams,0,sizeof(websrvparams));
  config_get( websrvoptions,sizeof(websrvoptions)/sizeof(paramdef_t),"websrv");
  
  
  
  if (ulfius_init_instance(&(websrvparams.instance), websrvparams.listenport, NULL, NULL) != U_OK) {
    LOG_W(UTIL, "[websrv] Error,cannot init websrv\n");
    return(NULL);
  }
  
  u_map_put(websrvparams.instance.default_headers, "Access-Control-Allow-Origin", "*");
  
  // Maximum body size sent by the client is 1 Kb
  websrvparams.instance.max_post_body_size = 1024;
  
  // Endpoint list declaration
  //1: load the frontend code: files contained in the websrvparams.url directory
  ulfius_add_endpoint_by_val(&(websrvparams.instance), "GET", websrvparams.url, NULL, 0, &websrv_callback_get_mainurl, NULL);
  //2: build the first page, when receiving the "oaisoftmodem" url 
//  ulfius_add_endpoint_by_val(&(websrvparams.instance), "GET", "oaisoftmodem", "variables", 0, &websrv_callback_get_softmodemstatus, NULL);
  ulfius_add_endpoint_by_val(&(websrvparams.instance), "GET", "oaisoftmodem", "commands", 0, &websrv_callback_get_softmodemmodules, NULL);

  //3 default_endpoint declaration, it tries to open the file with the url name as specified in the request.It looks for the file 
  ulfius_set_default_endpoint(&(websrvparams.instance), &websrv_callback_default, NULL);

  // endpoints 
  int (* callback_functions_var[3])(const struct _u_request * request, 
                                               struct _u_response * response,
                                               void * user_data) ={websrv_callback_get_softmodemstatus,websrv_callback_okset_softmodem_cmdvar,websrv_callback_set_softmodemvar};
  char *http_methods[3]={"GET","OPTIONS","POST"};

  websrv_add_endpoint(http_methods,3,"oaisoftmodem","variables" ,callback_functions_var,NULL);

  for (int i=0; telnetparams->CmdParsers[i].cmd != NULL; i++) {
	  register_module_endpoints( &(telnetparams->CmdParsers[i]) );
  }
  
  /* callbacks to take care of modules not yet initialized, so not visible in telnet server data when this autoinit runs: */
  ulfius_add_endpoint_by_val(&(websrvparams.instance), "GET", "oaisoftmodem", "@module/commands", 10, websrv_callback_newmodule, telnetparams);
  ulfius_add_endpoint_by_val(&(websrvparams.instance), "GET", "oaisoftmodem", "@module/variables", 10, websrv_callback_newmodule, telnetparams);

  // Start the framework
  ret=U_ERROR;
  if (websrvparams.keyfile!=NULL && websrvparams.certfile!=NULL) {
    char * key_pem = websrv_read_file(websrvparams.keyfile);
    char * cert_pem = websrv_read_file(websrvparams.certfile);
    if ( key_pem == NULL && cert_pem != NULL) {
      ret = ulfius_start_secure_framework(&(websrvparams.instance), key_pem, cert_pem);
      free(key_pem);
      free(cert_pem);
    } else {
      LOG_E(UTIL,"[websrv] Unable to load key %s and cert %s_\n",websrvparams.keyfile,websrvparams.certfile);
    }
  } else {
    ret = ulfius_start_framework(&(websrvparams.instance));
  }

  if (ret == U_OK) {
    LOG_I(UTIL, "[websrv] Web server started on port %d", websrvparams.instance.port);
  } else {
    LOG_W(UTIL,"[websrv] Error starting web server on port %d\n",websrvparams.instance.port);
  }
 return &(websrvparams.instance);

}

void websrv_end(void *webinst) {
  ulfius_stop_framework((struct _u_instance *)webinst);
  ulfius_clean_instance((struct _u_instance *)webinst);
  return;
}
