///
/// @file tbinx.c
/// @authors k. Headley
/// @date 01 jan 2018

/// TRN preprocess binary log re-transmit
/// reads binary packet format sent by mbtrnpreprocess
/// and transmits back to socket, csv file, and/or stdout

/////////////////////////
// Terms of use
/////////////////////////
/*
 Copyright Information
 
 Copyright 2000-2018 MBARI
 Monterey Bay Aquarium Research Institute, all rights reserved.
 
 Terms of Use
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version. You can access the GPLv3 license at
 http://www.gnu.org/licenses/gpl-3.0.html
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details
 (http://www.gnu.org/licenses/gpl-3.0.html)
 
 MBARI provides the documentation and software code "as is", with no warranty,
 express or implied, as to the software, title, non-infringement of third party
 rights, merchantability, or fitness for any particular purpose, the accuracy of
 the code, or the performance or results which you may obtain from its use. You
 assume the entire risk associated with use of the code, and you agree to be
 responsible for the entire cost of repair or servicing of the program with
 which you are using the code.
 
 In no event shall MBARI be liable for any damages, whether general, special,
 incidental or consequential damages, arising out of your use of the software,
 including, but not limited to, the loss or corruption of your data or damages
 of any kind resulting from use of the software, any prohibited use, or your
 inability to use the software. You agree to defend, indemnify and hold harmless
 MBARI and its officers, directors, and employees against any claim, loss,
 liability or expense, including attorneys' fees, resulting from loss of or
 damage to property or the injury to or death of any person arising out of the
 use of the software.
 
 The MBARI software is provided without obligation on the part of the
 Monterey Bay Aquarium Research Institute to assist in its use, correction,
 modification, or enhancement.
 
 MBARI assumes no responsibility or liability for any third party and/or
 commercial software required for the database or applications. Licensee agrees
 to obtain and maintain valid licenses for any additional third party software
 required.
 */
/////////////////////////
// Headers
/////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <arpa/inet.h>

#include "mlist.h"
#include "mdebug.h"
#include "mbtrn.h"
#include "iowrap.h"
#include "mbtrn-utils.h"

/////////////////////////
// Macros
/////////////////////////
#define TBINX_NAME "tbinx"
#ifndef TBINX_BUILD
/// @def TBINX_BUILD
/// @brief module build date.
/// Sourced from CFLAGS in Makefile
/// w/ -DMBTRN_BUILD=`date`
#define TBINX_BUILD ""VERSION_STRING(MBTRN_BUILD)
#endif

// These macros should only be defined for
// application main files rather than general C files
/*
 /// @def PRODUCT
 /// @brief header software product name
 #define PRODUCT "MBRT"
 
 /// @def COPYRIGHT
 /// @brief header software copyright info
 #define COPYRIGHT "Copyright 2002-2013 MBARI Monterey Bay Aquarium Research Institute, all rights reserved."
 /// @def NOWARRANTY
 /// @brief header software terms of use
 #define NOWARRANTY  \
 "This program is distributed in the hope that it will be useful,\n"\
 "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"\
 "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"\
 "GNU General Public License for more details (http://www.gnu.org/licenses/gpl-3.0.html)\n"
 */

/// @def ID_APP
/// @brief debug module ID
#define ID_APP 1
/// @def ID_V1
/// @brief debug module ID
#define ID_V1 2
/// @def ID_V2
/// @brief debug module ID
#define ID_V2 3
/// @def ID_V3
/// @brief debug module ID
#define ID_V3 4

/////////////////////////
// Declarations
/////////////////////////
/// @typedef enum oflags_t oflags_t
/// @brief flags specifying output types
typedef enum{OF_NONE=0,OF_SOUT=0x1,OF_CSV=0x2, OF_SOCKET=0x4, OF_SERR=0x10}oflags_t;

/// @def TBX_MAX_VERBOSE
/// @brief max verbose output level
#define TBX_MAX_VERBOSE 3
/// @def TBX_MSG_CON_LEN
/// @brief TRN connect message length
#define TBX_MSG_CON_LEN 4
/// @def TBX_HBTOK_DFL
/// @brief default heartbeat interval (messages)
#define TBX_HBTOK_DFL 50
/// @def TBX_MAX_DELAY_SEC
/// @brief max message output delay
#define TBX_MAX_DELAY_SEC 3
/// @def TBX_MIN_DELAY_SEC
/// @brief minimum message output delay (s)
#define TBX_MIN_DELAY_SEC 0
/// @def TBX_MIN_DELAY_NSEC
/// @brief minimum message output delay (ns)
#define TBX_MIN_DELAY_NSEC 8000000
/// @def TBX_SOCKET_DELAY_SEC
/// @brief time to wait before socket retry
/// if no clients connected
#define TBX_SOCKET_DELAY_SEC 3
/// @def TBX_VERBOSE_DFL
/// @brief verbose output default
#define TBX_VERBOSE_DFL 0
/// @def TBX_NFILES_DFL
/// @brief default number of files
#define TBX_NFILES_DFL 0
/// @def TBX_OFLAGS_DFL
/// @brief default output flags
#define TBX_OFLAGS_DFL OF_SOUT
/// @def TBX_HOST_DFL
/// @brief default host
#define TBX_HOST_DFL "localhost"
/// @def TBX_PORT_DFL
/// @brief default output port
#define TBX_PORT_DFL 27000
/// @def TBX_DELAY_DFL
/// @brief default output delay
#define TBX_DELAY_DFL 0

/// @typedef struct app_cfg_s app_cfg_t
/// @brief application configuration parameter structure
typedef struct app_cfg_s{
    /// @var app_cfg_s::verbose
    /// @brief enable verbose output
    int verbose;
    /// @var app_cfg_s::nfiles
    /// @brief file list
     int nfiles;
    /// @var app_cfg_s::files
    /// @brief file list
    char **files;
    /// @var app_cfg_s::oflags
    /// @brief output type flags
    oflags_t oflags;
    /// @var app_cfg_s::csv_path
    /// @brief csv file name
    char *csv_path;
    /// @var app_cfg_s::host
    /// @brief host
    char *host;
    /// @var app_cfg_s::port
    /// @brief port
    int port;
    /// @var app_cfg_s::delay_msec
    /// @brief packet delay.
    /// -1: no delay
    ///  0: use timestamps (default)
    /// >0: use specified value
    int delay_msec;
 
}app_cfg_t;

static void s_show_help();

/////////////////////////
// Imports
/////////////////////////

/////////////////////////
// Module Global Variables
/////////////////////////

static iow_socket_t *trn_osocket = NULL;
static iow_peer_t *trn_peer = NULL;
static mlist_t *trn_plist = NULL;
static int trn_hbtok = TBX_HBTOK_DFL;
static int trn_tx_count=0;
static int trn_rx_count=0;
static int trn_tx_bytes=0;
static int trn_rx_bytes=0;
static int trn_msg_count=0;
static int trn_msg_bytes=0;
static int trn_cli_con=0;
static int trn_cli_dis=0;

/////////////////////////
// Function Definitions
/////////////////////////

/// @fn void s_show_help()
/// @brief output user help message to stdout.
/// @return none
static void s_show_help()
{
    char help_message[] = "\nmbtrnpreprocess binary log emitter\n";
    char usage_message[] = "\ntbinx [options]\n"
    "--verbose=n      : verbose output, n>0\n"
    "--help           : output help message\n"
    "--version        : output version info\n"
    "--socket=host:port : export to socket\n"
    "--sout           : export to stdout\n"
    "--serr           : export to stderr\n"
    "--csv=file       : export to csv file\n"
    "--delay=msec     : minimum packet delay [0:use timestamps (default), -1:no delay]\n"
    "\n";
    printf("%s",help_message);
    printf("%s",usage_message);
}
// End function s_show_help

/// @fn void parse_args(int argc, char ** argv, app_cfg_t * cfg)
/// @brief parse command line args, set application configuration.
/// @param[in] argc number of arguments
/// @param[in] argv array of command line arguments (strings)
/// @param[in] cfg application config structure
/// @return none
static void parse_args(int argc, char **argv, app_cfg_t *cfg)
{
    extern char WIN_DECLSPEC *optarg;
    int option_index;
    int c;
    bool help=false;
    bool version=false;
    
    static struct option options[] = {
        {"verbose", required_argument, NULL, 0},
        {"help", no_argument, NULL, 0},
        {"version", no_argument, NULL, 0},
        {"sout", no_argument, NULL, 0},
        {"serr", no_argument, NULL, 0},
        {"socket", required_argument, NULL, 0},
        {"csv", required_argument, NULL, 0},
        {"delay", required_argument, NULL, 0},
       {NULL, 0, NULL, 0}};

    /* process argument list */
    while ((c = getopt_long(argc, argv, "", options, &option_index)) != -1){
        switch (c) {
                // long options all return c=0
            case 0:
                // verbose
                if (strcmp("verbose", options[option_index].name) == 0) {
                    sscanf(optarg,"%d",&cfg->verbose);
                }
                // help
                else if (strcmp("help", options[option_index].name) == 0) {
                    help = true;
                }
                // version
                else if (strcmp("version", options[option_index].name) == 0) {
                    version = true;
                }
                // sout
                else if (strcmp("sout", options[option_index].name) == 0) {
                    cfg->oflags|=OF_SOUT;
                }
                // serr
                else if (strcmp("serr", options[option_index].name) == 0) {
                    cfg->oflags |= OF_SERR;
                    cfg->oflags &= ~OF_SOUT;
                }
                // socket
                else if (strcmp("socket", options[option_index].name) == 0) {
                    cfg->oflags|=OF_SOCKET;
                    cfg->oflags &= ~OF_SOUT;
                    
                    char *ocopy=strdup(optarg);
                    cfg->host=strtok(ocopy,":");
                    if (cfg->host==NULL) {
                        cfg->host=TBX_HOST_DFL;
                    }
                    char *ip = strtok(NULL,":");
                    if (ip!=NULL) {
                        sscanf(ip,"%d",&cfg->port);
                    }
                }
                // csv
                else if (strcmp("csv", options[option_index].name) == 0) {
                    if (cfg->csv_path) {
                        free(cfg->csv_path);
                    }
                    cfg->oflags|=OF_CSV;
                    cfg->csv_path=strdup(optarg);
                }
                // delay
                else if (strcmp("delay", options[option_index].name) == 0) {
                    sscanf(optarg,"%d",&cfg->delay_msec);
                }
               break;
            default:
                help=true;
                break;
        }
        if (version) {
            mbtrn_show_app_version(TBINX_NAME,TBINX_BUILD);
            exit(0);
        }
        if (help) {
            mbtrn_show_app_version(TBINX_NAME,TBINX_BUILD);
            s_show_help();
            exit(0);
        }
    }// while
    
    if (cfg->verbose>TBX_MAX_VERBOSE) {
        cfg->verbose=TBX_MAX_VERBOSE;
    }
    if (cfg->verbose<0) {
        cfg->verbose=0;
    }


    cfg->files=&argv[optind];
    cfg->nfiles=argc-optind;
    
    // initialize reader
    // create and open socket connection

    mdb_set_name(ID_APP,"mbtrnpreprocess");
    mdb_set_name(ID_V1,"verbose_1");
    mdb_set_name(ID_V2,"verbose_2");
    mdb_set_name(ID_V3,"verbose_3");
    
    // use MDI_ALL to globally set module debug output
    // may also set per module basis using module IDs
    // defined in mconfig.h:
    // ID_APP, ID_V1, ID_V2, ID_V3,
    // valid level values are
    // MDL_UNSET,MDL_NONE
    // MDL_FATAL, MDL_ERROR, MDL_WARN
    // MDL_INFO, MDL_DEBUG
    
    mdb_set(MDI_ALL,MDL_NONE);

    switch (cfg->verbose) {
        case 0:
            mdb_set(ID_APP,MDL_ERROR);
            break;
        case 1:
            mdb_set(ID_APP,MDL_DEBUG);
            mdb_set(ID_V1,MDL_DEBUG);
            break;
        case 2:
            mdb_set(ID_APP,MDL_DEBUG);
            mdb_set(ID_V1,MDL_DEBUG);
            mdb_set(ID_V2,MDL_DEBUG);
            break;
        case 3:
            mdb_set(ID_APP,MDL_DEBUG);
            mdb_set(ID_V1,MDL_DEBUG);
            mdb_set(ID_V2,MDL_DEBUG);
            mdb_set(ID_V3,MDL_DEBUG);
            break;

        default:
            mdb_set(ID_APP,MDL_INFO);
            break;
    }
    if (cfg->verbose!=0) {
        fprintf(stderr,"verbose   [%s]\n",(cfg->verbose?"Y":"N"));
        fprintf(stderr,"nfiles    [%d]\n",cfg->nfiles);
        if (cfg->nfiles>0) {
            for (int i=0; i<cfg->nfiles; i++) {
                fprintf(stderr,"files[%2d] [%s]\n",i,cfg->files[i]);
            }
        }
        fprintf(stderr,"sout      [%c]\n",(cfg->oflags&OF_SOUT?'Y':'N'));
        fprintf(stderr,"csv       [%c]\n",(cfg->oflags&OF_CSV?'Y':'N'));
        fprintf(stderr,"socket    [%c]\n",(cfg->oflags&OF_SOCKET?'Y':'N'));
        if (cfg->oflags&OF_SOCKET) {
        fprintf(stderr,"host:port [%s:%d]\n",cfg->host,cfg->port);
        }
        fprintf(stderr,"delay     [%d]\n",(cfg->delay_msec));
    }

}
// End function parse_args

/// @fn int s_delay_message(trn_data_t *message)
/// @brief delay packet per packet timestamp
/// @param[in] message message reference
/// @param[in] prev_time previous message timestamp
/// @param[in] cfg app configuration reference
/// @return 0 on success, -1 otherwise
static int s_delay_message(trn_message_t *message, double prev_time, app_cfg_t *cfg)
{
    int retval=-1;
    
    if (NULL!=message && NULL!=cfg) {
        
        double tsdiff = 0.0;
        struct timespec delay={0};
        struct timespec rem={0};
        
        if (cfg->delay_msec==0) {
            // use timestamps
            tsdiff = (message->data.sounding.ts-prev_time);
            
            MMDEBUG(ID_V3,"prev_time[%.3lf] ts[%.3lf] tsdiff[%.3lf]\n",prev_time,message->data.sounding.ts,tsdiff);
            if (tsdiff>TBX_MAX_DELAY_SEC) {
                // if delay too large, use min delay
                delay.tv_sec=TBX_MIN_DELAY_SEC;
                delay.tv_nsec=TBX_MIN_DELAY_NSEC;//0;//800000;
                MMDEBUG(ID_V3,"case >max - using min delay[%"PRIu32":%"PRIu32"]\n",(uint32_t)delay.tv_sec,(uint32_t)delay.tv_nsec);
            }else{
                if (  prev_time>0 && tsdiff > 0) {
                    time_t lsec = (time_t)tsdiff;
                    long lnsec = (10000000L*(tsdiff-lsec));
                    struct timespec rem={0};
                    delay.tv_sec=lsec;
                    delay.tv_nsec=lnsec;
                    MMDEBUG(ID_V3,"case ts - using delay[%"PRIu32":%"PRIu32"]\n",(uint32_t)delay.tv_sec,(uint32_t)delay.tv_nsec);
                    
                    while (nanosleep(&delay,&rem)<0) {
                        MMDEBUG(ID_V3,"sleep interrupted\n");
                        delay.tv_sec=rem.tv_sec;
                        delay.tv_nsec=rem.tv_nsec;
                    }
                }else{
                    // if delay <0, use min delay
                    delay.tv_sec=TBX_MIN_DELAY_SEC;
                    delay.tv_nsec=TBX_MIN_DELAY_NSEC;//0;//800000;
                    MMDEBUG(ID_V3,"case ts<0 - using min delay[%"PRIu32":%"PRIu32"]\n",(uint32_t)delay.tv_sec,(uint32_t)delay.tv_nsec);
                }
            }
        }else if (cfg->delay_msec>0){
            // use specified delay
            uint64_t dsec=cfg->delay_msec/1000;
            uint64_t dmsec=cfg->delay_msec%1000;
            delay.tv_sec=dsec;
            delay.tv_nsec=dmsec*1000000L;
            MMDEBUG(ID_V3,"case specified - using delay[%"PRIu32":%"PRIu32"]\n",(uint32_t)delay.tv_sec,(uint32_t)delay.tv_nsec);
        }else{
            // else no delay/min delay
            // with zero delay, client REQ missed/arrive late - why?
           delay.tv_sec=TBX_MIN_DELAY_SEC;
            delay.tv_nsec=TBX_MIN_DELAY_NSEC;//0;//800000;
            MMDEBUG(ID_V3,"case <0 - using min delay[%"PRIu32":%"PRIu32"]\n",(uint32_t)delay.tv_sec,(uint32_t)delay.tv_nsec);
        }

        if (delay.tv_sec>0 || delay.tv_nsec>0) {
            
            while(nanosleep(&delay,&rem)<0){
                delay.tv_sec=rem.tv_sec;
                delay.tv_nsec=rem.tv_nsec;
            }
        }
        
        retval=0;
    }
    return retval;
}
// End function s_delay_message

/// @fn int s_out_sout(trn_data_t *message)
/// @brief export message to stdout
/// @param[in] message message reference
/// @return 0 on success, -1 otherwise
static int s_out_sout(trn_message_t *message)
{
    int retval=0;
    
    if (NULL!=message) {
        mbtrn_sounding_t *psounding     = &message->data.sounding;
        fprintf(stdout,"\nts[%.3lf] ping[%06"PRIu32"] beams[%03d]\nlat[%.4f] lon[%.4lf] hdg[%6.2lf] sd[%7.2lf]\n",\
                psounding->ts,
                psounding->ping_number,
                psounding->nbeams,
                psounding->lat,
                psounding->lon,
                psounding->hdg,
                psounding->depth
                );
        if (psounding->nbeams<=512) {
            for (int j = 0; j < (int)psounding->nbeams; j++) {
                fprintf(stdout,"n[%03"PRId32"] atrk/X[%+10.3lf] ctrk/Y[%+10.3lf] dpth/Z[%+10.3lf]\n",
                        psounding->beams[j].beam_num,
                        psounding->beams[j].rhox,
                        psounding->beams[j].rhoy,
                        psounding->beams[j].rhoz);
            }
        }
    }else{
        MMDEBUG(ID_V1,"invalid argument\n");
        retval=-1;
    }
    return retval;
}
// End function s_out_sout

/// @fn int s_out_serr(trn_data_t *message)
/// @brief export message to stderr
/// @param[in] message message reference
/// @return 0 on success, -1 otherwise
static int s_out_serr(trn_message_t *message)
{
    int retval=0;
    
    if (NULL!=message) {
        mbtrn_sounding_t *psounding     = &message->data.sounding;
        fprintf(stderr,"\nts[%.3lf] ping[%06"PRIu32"] beams[%03d]\nlat[%.4f] lon[%.4lf] hdg[%6.2lf] sd[%7.2lf]\n",\
                psounding->ts,
                psounding->ping_number,
                psounding->nbeams,
                psounding->lat,
                psounding->lon,
                psounding->hdg,
                psounding->depth
                );
        if (psounding->nbeams<=512) {
            for (int j = 0; j < (int)psounding->nbeams; j++) {
                fprintf(stderr,"n[%03"PRId32"] atrk/X[%+10.3lf] ctrk/Y[%+10.3lf] dpth/Z[%+10.3lf]\n",
                        psounding->beams[j].beam_num,
                        psounding->beams[j].rhox,
                        psounding->beams[j].rhoy,
                        psounding->beams[j].rhoz);
            }
        }
    }else{
        MMDEBUG(ID_V1,"invalid argument\n");
        retval=-1;
    }
    return retval;
}
// End function s_out_serr

/// @fn int s_out_csv(trn_data_t *message)
/// @brief export message to csv file
/// @param[in] dest CSV file reference
/// @param[in] message message reference
/// @return 0 on success, -1 otherwise
static int s_out_csv(iow_file_t *dest, trn_message_t *message)
{
    int retval=0;
    
    if (NULL!=message) {
        mbtrn_sounding_t *psounding     = &message->data.sounding;
        iow_fprintf(dest,"%.3lf,%"PRIu32",%d,%lf,%lf,%lf,%lf",\
                    psounding->ts,
                    psounding->ping_number,
               psounding->nbeams,
               psounding->lat,
               psounding->lon,
               psounding->hdg,
               psounding->depth
               );
        for (int j = 0; j < (int)psounding->nbeams; j++) {
            iow_fprintf(dest,",%d,%+lf,%+lf,%+lf",
                   psounding->beams[j].beam_num,
                   psounding->beams[j].rhox,
                   psounding->beams[j].rhoy,
                   psounding->beams[j].rhoz);
        }
        iow_fprintf(dest,"\n");
    }else{
        MMDEBUG(ID_V1,"invalid argument\n");
        retval=-1;
    }
    return retval;
}
// End function s_out_csv

/// @fn int s_out_socket(trn_data_t *message)
/// @brief export message to socket
/// @param[in] s socket reference
/// @param[in] message message reference
/// @return 0 on success, -1 otherwise
static int s_out_socket(iow_socket_t *s, trn_message_t *message)
{
    int retval=-1;
    
    if (NULL!=message) {
        
        byte cmsg[TBX_MSG_CON_LEN];
        int iobytes = 0;
        int idx=-1;
        int svc=0;
        iow_peer_t *psub=mlist_head(trn_plist);
        bool data_available=false;
        
        // when socket output is enabled,
        // wait until a client connects
        // Otherwise, the data will just fall on the floor
        
        do{
            // check the socket for client activity
            MMDEBUG(ID_APP,"checking TRN host socket\n");
            iobytes = iow_recvfrom(s, trn_peer->addr, cmsg, TBX_MSG_CON_LEN);
            
            switch (iobytes) {
                case 0:
                    MMINFO(ID_APP,"err - recvfrom ret 0 (socket closed) removing cli[%d]\n",trn_peer->id);
                    // socket closed, remove client from list
                    if(sscanf(trn_peer->service,"%d",&svc)==1){
                        iow_peer_t *peer = (iow_peer_t *)mlist_vlookup(trn_plist, &svc, mbtrn_peer_vcmp);
                        if (peer!=NULL) {
                            mlist_remove(trn_plist,peer);
                        }
                    }
                    data_available=true;
                    break;
                case -1:
                    // nothing to read - bail out
                    MMDEBUG(ID_APP,"err - recvfrom cli[%d] ret -1 [%d/%s]\n",trn_peer->id,errno,strerror(errno));
                    data_available=false;
                    break;
                    
                default:
                    
                    // client sent something
                    // update stats
                    trn_rx_count++;
                    trn_rx_bytes+=iobytes;
                    data_available=true;
                    
                    const char *ctest=NULL;
                    uint16_t port=0xFFFF;
                    struct sockaddr_in *psin = NULL;
                    
                    if (NULL != trn_peer->addr &&
                        NULL != trn_peer->addr->ainfo &&
                        NULL != trn_peer->addr->ainfo->ai_addr) {
                        // get client endpoint info
                        psin = (struct sockaddr_in *)trn_peer->addr->ainfo->ai_addr;
                        ctest = inet_ntop(AF_INET, &psin->sin_addr, trn_peer->chost, IOW_ADDR_LEN);
                        if (NULL!=ctest) {
                            
                            port = ntohs(psin->sin_port);
                            
                            svc = port;
                            snprintf(trn_peer->service,NI_MAXSERV,"%d",svc);
                            
                            iow_peer_t *pclient=NULL;
                            
                            // check client list
                            // to see whether already connected
                            pclient = (iow_peer_t *)mlist_vlookup(trn_plist,&svc,mbtrn_peer_vcmp);
                            if (pclient!=NULL) {
                                // client exists, update heartbeat tokens
                                // [could make additive, i.e. +=]
                                MMDEBUG(ID_APP,"updating client hbeat id[%d] addr[%p]\n",svc,trn_peer);
                                pclient->heartbeat = trn_hbtok;
                            }else{
                                MMDEBUG(ID_APP,"adding to client list id[%d] addr[%p]\n",svc,trn_peer);
                                // client doesn't exist
                                // initialze and add to list
                                trn_peer->id = svc;
                                trn_peer->heartbeat = trn_hbtok;
                                trn_peer->next=NULL;
                                mlist_add(trn_plist, (void *)trn_peer);
                                // save pointer to finish up (send ACK, update hbeat)
                                pclient=trn_peer;
                                // create a new peer for next connection
                                trn_peer = iow_peer_new();
                                
                                trn_cli_con++;
                            }
                            
                            MMDEBUG(ID_APP,"rx [%d]b cli[%d/%s:%s]\n", iobytes, svc, trn_peer->chost, trn_peer->service);

                            // send ACK
                            iobytes = iow_sendto(s, pclient->addr, (byte *)"ACK", 4, 0 );
                            if ( (NULL!=pclient) &&  (iobytes> 0) ) {
                                MMDEBUG(ID_APP,"tx ACK [%d]b cli[%d/%s:%s]\n",iobytes, svc, pclient->chost, pclient->service);
                                trn_tx_count++;
                                trn_tx_bytes+=iobytes;
                            }else{
                                fprintf(stderr,"tx cli[%d] failed pclient[%p] iobytes[%d] [%d/%s]\n",svc,pclient,iobytes,errno,strerror(errno));
                            }
                        }else{
                            MMERROR(ID_APP,"err - inet_ntop failed [%d/%s]\n",errno,strerror(errno));
                        }
                    }else{
                        // invalid sockaddr
                        MMERROR(ID_APP,"err - NULL cliaddr(rx) cli[%d]\n",trn_peer->id);
                        fprintf(stderr,"err - NULL cliaddr(rx) cli[%d]\n",trn_peer->id);
                    }
                    
                    break;
            }
            // keep reading while data is availble
        } while (data_available);
        
        
        // send output to clients
        psub = (iow_peer_t *)mlist_first(trn_plist);
        idx=0;
        while (psub != NULL) {
            
            psub->heartbeat--;
            
            uint32_t message_len=(MBTRN_HEADER_SIZE+MBTRN_FIXED_SIZE+MBTRN_BEAM_BYTES*message->data.sounding.nbeams+MBTRN_CHKSUM_BYTES);//(4+(5*8)+(3*4)+(8*message->hdr.bcount));
            if ( (iobytes = iow_sendto(trn_osocket, psub->addr, (byte *)message, message_len, 0 )) > 0) {
                trn_tx_count++;
                trn_tx_bytes+=iobytes;
                trn_msg_bytes+=iobytes;
                trn_msg_count++;
                retval=0;

                MMDEBUG(ID_APP,"tx TRN [%5d]b cli[%d/%s:%s] hb[%d]\n",
                        iobytes, idx, psub->chost, psub->service, psub->heartbeat);
                
            }else{
                MERROR("err - sendto ret[%d] cli[%d] [%d/%s]\n",iobytes,idx,errno,strerror(errno));
            }
            // check heartbeat, remove expired peers
            if (NULL!=psub && psub->heartbeat==0) {
                MMDEBUG(ID_APP,"hbeat=0 cli[%d/%d] - removed\n",idx,psub->id);
                mlist_remove(trn_plist,psub);
                trn_cli_dis++;
            }
            psub=(iow_peer_t *)mlist_next(trn_plist);
            idx++;
        }// while psub
        
        
    }else{
        MMDEBUG(ID_V1,"invalid argument\n");
        retval=-1;
    }
    
    // return -1 if message unsent
    return retval;
}
// End function s_out_socket

/// @fn int s_process_file(char *file)
/// @brief process TRN message file, send to specified outputs.
/// @param[in] cfg app configuration reference
/// @return 0 on success, -1 otherwise
int s_process_file(app_cfg_t *cfg)
{
    int retval=0;
    
   if (NULL!=cfg && cfg->nfiles>0) {
        for (int i=0; i<cfg->nfiles; i++) {
            MMDEBUG(ID_V1,"processing %s\n",cfg->files[i]);
            iow_file_t *ifile = iow_file_new(cfg->files[i]);
            int test=0;
            iow_file_t *csv_file = NULL;
            
            if(cfg->csv_path!=NULL){
                csv_file=iow_file_new(cfg->csv_path);
                if ( (test=iow_mopen(csv_file,IOW_RDWR|IOW_CREATE,IOW_RU|IOW_WU|IOW_RG|IOW_WG)) <= 0) {
                    MMERROR(ID_APP,"could not open CSV file\n");
                    csv_file=NULL;
                }else{
                    MMDEBUG(ID_APP,"opened CSV file [%s]\n",cfg->csv_path);
                }
            }
            
            if ((cfg->oflags&OF_SOCKET) ) {
                trn_peer=iow_peer_new();
                trn_plist = mlist_new();
                mlist_autofree(trn_plist,iow_peer_free);

                trn_osocket = iow_socket_new(cfg->host, cfg->port, ST_UDP);
                iow_set_blocking(trn_osocket,false);
                int test=-1;
                if ( (test=iow_bind(trn_osocket))==0) {
                    fprintf(stderr,"TRN host socket bind OK [%s:%d]\n",cfg->host, cfg->port);
                }else{
                    fprintf(stderr, "\nTRN host socket bind failed [%d] [%d %s]\n",test,errno,strerror(errno));
                }
            }
            
            if ( (test=iow_open(ifile,IOW_RONLY)) > 0) {
                MMDEBUG(ID_V1,"open OK [%s]\n",cfg->files[i]);
                
                trn_message_t message;
                memset(&message,0,sizeof(trn_message_t));

                trn_message_t *pmessage  = &message;
                mbtrn_header_t *phdr     = &pmessage->data.header;
                mbtrn_sounding_t *psounding     = &pmessage->data.sounding;
                byte *ptype = (byte *)&phdr->type;
                byte *psize = (byte *)&phdr->size;
                double prev_time =0.0;
                
                bool ferror=false;
                int64_t rbytes=0;
                bool header_valid=false;
                bool sounding_valid=false;
                bool rec_valid=false;
                while (!ferror) {
                    byte *sp = (byte *)&phdr->type;
                    header_valid=false;
                    sounding_valid=false;
                    rec_valid=false;
                    while (!sounding_valid) {
                        if( ((rbytes=iow_read(ifile,(byte *)sp,1))==1) && *sp=='M'){
                            sp++;
                            if( ((rbytes=iow_read(ifile,(byte *)sp,1))==1) && *sp=='B'){
                                sp++;
                                if( ((rbytes=iow_read(ifile,(byte *)sp,1))==1) && *sp=='1'){
                                    sp++;
                                    if( ((rbytes=iow_read(ifile,(byte *)sp,1))==1) && *sp=='\0'){
                                        MMDEBUG(ID_V1,"sync read slen[%d]\n",MBTRN_HEADER_TYPE_BYTES);
                                        MMDEBUG(ID_V2,"  sync     ['%c''%c''%c''%c']/[%02X %02X %02X %02X]\n",
                                                ptype[0],
                                                ptype[1],
                                                ptype[2],
                                                ptype[3],
                                                ptype[0],
                                                ptype[1],
                                                ptype[2],
                                                ptype[3]);
                                        if( ((rbytes=iow_read(ifile,(byte *)psize,4))==4)){
                                            // read size
                                    		header_valid=true;
                                        }
                                        break;
                                    }else{
                                        sp=ptype;
                                    }
                                }else{
                                    sp=ptype;
                                }
                            }else{
                                sp=ptype;
                            }
                        }
                        if(rbytes<=0){
                            MMDEBUG(ID_APP,"reached EOF looking for sync\n");
                            ferror=true;
                            break;
                        }
                    }
                    if (header_valid && !ferror) {
                        
                        byte *psnd = (byte *)psounding;

                        if( ((rbytes=iow_read(ifile,psnd,MBTRN_FIXED_SIZE))==MBTRN_FIXED_SIZE)){
                            
                            int32_t cmplen = MBTRN_FIXED_SIZE+MBTRN_HEADER_SIZE+psounding->nbeams*MBTRN_BEAM_BYTES+MBTRN_CHKSUM_BYTES;
                            
                            if ((int32_t)phdr->size == cmplen ) {
                                header_valid=true;
                                MMDEBUG(ID_V1,"sounding header read len[%"PRIu32"/%lld]\n",(uint32_t)MBTRN_FIXED_SIZE,rbytes);
                                MMDEBUG(ID_V2,"  size   [%d]\n",phdr->size);
                                MMDEBUG(ID_V2,"  time   [%.3f]\n",psounding->ts);
                                MMDEBUG(ID_V2,"  lat    [%.3f]\n",psounding->lat);
                                MMDEBUG(ID_V2,"  lon    [%.3f]\n",psounding->lon);
                                MMDEBUG(ID_V2,"  depth  [%.3f]\n",psounding->depth);
                                MMDEBUG(ID_V2,"  hdg    [%.3f]\n",psounding->hdg);
                                MMDEBUG(ID_V2,"  ping   [%06d]\n",psounding->ping_number);
                                MMDEBUG(ID_V2,"  nbeams [%d]\n",psounding->nbeams);
                            }else{
                                MMWARN(ID_APP,"message len invalid l[%d] l*[%d]\n",phdr->size,cmplen);
                            }
                        }else{
                            MMERROR(ID_APP,"could not read header bytes [%lld]\n",rbytes);
                            ferror=true;
                        }
                    }
                    
                   byte *cp = (byte *)&pmessage->chksum;
                    
                    if (header_valid && ferror==false && psounding->nbeams>0) {
                        
                        int32_t beamsz = psounding->nbeams*MBTRN_BEAM_BYTES;
                        mbtrn_beam_data_t *pbeams = &psounding->beams[0];
                        
                        if( NULL != pbeams ){
                            
                            byte *bp = (byte *)pbeams;
                            cp = (byte *)&pmessage->chksum;

                            if( ((rbytes=iow_read(ifile,bp,beamsz))==beamsz)){
                                
//                                MMDEBUG(ID_V1,"beams read blen[%d/%lld]\n",beamsz,rbytes);
                                
                                if( ((rbytes=iow_read(ifile,cp,MBTRN_CHKSUM_BYTES))==MBTRN_CHKSUM_BYTES)){
//                                    MMDEBUG(ID_V1,"chksum read clen[%lld]\n",rbytes);
//                                    MMDEBUG(ID_V2,"  chksum [%0X]\n",pmessage->chksum);

                                    rec_valid=true;
                                }else{
                                    MMWARN(ID_APP,"chksum read failed [%lld]\n",rbytes);
                                }
                                
                            }else{
                                MMDEBUG(ID_V1,"beam read failed pb[%p] read[%lld]\n",pbeams,rbytes);
                            }
                            
                        }else{
                            MMERROR(ID_APP,"beam data malloc failed\n");
                        }
                    }else{
                        if( ((rbytes=iow_read(ifile,cp,MBTRN_CHKSUM_BYTES))==MBTRN_CHKSUM_BYTES)){
//                            MMDEBUG(ID_V1,"read chksum clen[%lld]\n",rbytes);
//                            MMDEBUG(ID_V2,"  chksum [%0X]\n",pmessage->chksum);
                            
                            rec_valid=true;
                        }else{
                            MMWARN(ID_APP,"chksum read failed [%lld]\n",rbytes);
                        }
                    }
                    
                    if (rec_valid && ferror==false) {


                        s_delay_message(pmessage, prev_time, cfg);
                        prev_time=pmessage->data.sounding.ts;
                        
                        if (cfg->oflags&OF_SOUT) {
                            s_out_sout(pmessage) ;
                        }
                        if (cfg->oflags&OF_SERR) {
                            s_out_serr(pmessage) ;
                        }
                        if ( (cfg->oflags&OF_CSV) && (NULL!=csv_file) ) {
                            s_out_csv(csv_file,pmessage);
                        }
                        if ( (cfg->oflags&OF_SOCKET) && (NULL!=trn_osocket) ) {
                            int test=0;
                            do{
                                // send message to socket
                                // or wait until clients connected
                                if( (test=s_out_socket(trn_osocket,pmessage)) != 0 ){
                                    sleep(TBX_SOCKET_DELAY_SEC);
                                }
                            }while (test!=0);
                        }
                    }
                }
                iow_close(ifile);
                iow_file_destroy(&ifile);
                iow_file_destroy(&csv_file);
            }else{
                MMERROR(ID_APP,"file open failed[%s] [%d/%s]\n",cfg->files[i],errno,strerror(errno));
            }
        }
    }
    
    MMDEBUG(ID_APP,"tx count/bytes[%d/%d]\n",trn_tx_count,trn_tx_bytes);
    MMDEBUG(ID_APP,"rx count/bytes[%d/%d]\n",trn_rx_count,trn_rx_bytes);
    MMDEBUG(ID_APP,"trn count/bytes[%d/%d]\n",trn_msg_count,trn_msg_bytes);
    return retval;
}
// End function s_process_file

/// @fn int main(int argc, char ** argv)
/// @brief TRN test client main entry point.
/// may specify arguments on command line:
/// host   UDP server host (MB System host)
/// port   UDP server port (MB System TRN output port)
/// block  use blocking IO
/// cycles number of cycles (<=0 to run indefinitely)
/// bsize  buffer size
/// @param[in] argc number of command line arguments
/// @param[in] argv array of command line arguments (strings)
/// @return 0 on success, -1 otherwise
int main(int argc, char **argv)
{
    int retval=0;
    
    // set default app configuration
    app_cfg_t cfg = {TBX_VERBOSE_DFL,TBX_NFILES_DFL,NULL,
        TBX_OFLAGS_DFL,NULL,
        TBX_HOST_DFL,TBX_PORT_DFL,
        TBX_DELAY_DFL};
    
    app_cfg_t *pcfg = &cfg;
    
    if (argc<2) {
        s_show_help();
    }else{
        // parse command line args (update config)
        parse_args(argc, argv, pcfg);
        
        s_process_file(pcfg);
    }

    free(pcfg->csv_path);
    return retval;
}
// End function main

