/*
 * micloud.c
 *
 *  Created on: 2020年10月24日
 *      Author: kang
 */


/*
 * header
 */
//system header
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <miot_time.h>
#include <mi_ipc_api.h>
#include <mi_cloud_api.h>
#include <rpc_client.h>
#include <mi_cloud_porting.h>
#include <miot_log.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/realtek/realtek_interface.h"
//server header
#include "micloud.h"
#include "micloud_interface.h"
#include "../../tools/config/rwio.h"
#include "../../tools/log.h"
#include "config.h"
//#include "commom.h"
/*
 * static
 */
//variable
static server_info_t 		info;
static micloud_config_t		micloud_config;
static message_buffer_t		message;
static mi_alarm_config_t g_alarm_cfg = {0};
//static mi_cloud_config_t g_config = {0};
static pthread_rwlock_t		m_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t		m_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t		m_cond = PTHREAD_COND_INITIALIZER;
static int time_count=0;

static RPC_HANDLE_T g_rpc_handle = NULL;
//function
//common
static void *server_func(void *arg);
static int server_message_proc(void);
static void task_default(void);
//static void task_error(void);
static int server_release(void);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static void server_thread_termination(void);
//specific
static int stream_init(int quality);
static void str2hex(char *ds, unsigned char *bs, unsigned int n);
static int micloud_ipc_sdk_init(void);
static int micloud_init(void);
static int alarm_config_update(void);
static int miio_initiative_reported();
static int video_audio_cmd(int cmd_switch);
static int micloud_put_frame_puse_timer(void);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */
/*
 * helper
 */

void *frame_puse_thread(void *arg)
{
	pthread_detach(pthread_self());
		while(!info.exit)
		{
			if(time_count==3600)
			{
				video_audio_cmd(0);
				main_thread_exit_termination(1);
				misc_set_bit(&info.init_status, PUT_FRAME_PAUE_FLAG, 1);
				time_count=0;
				break;
			}
				sleep(1);
				time_count++;
		}
		log_qcy(DEBUG_INFO,"-----------thread exit: frame_puse_thread----micloud-------\n");
		pthread_exit(0);
}
static int micloud_put_frame_puse_timer(void)
{
	int ret;
	pthread_t tmp_id;
	ret=pthread_create(&tmp_id,NULL,frame_puse_thread,NULL);
	if(ret<0)
	{
		log_qcy(DEBUG_INFO, "creat  frame_puse_thread failed ret=%\n",ret);
		video_audio_cmd(0);
		main_thread_exit_termination(1);
		misc_set_bit(&info.init_status, PUT_FRAME_PAUE_FLAG, 1);
	}
	return ret;

}
static int video_audio_cmd(int cmd_switch)
{
 	message_t msg;
 	int ret;
 	if(cmd_switch)
 	{
	msg_init(&msg);
	msg.message = MSG_VIDEO_START;
	msg.sender = msg.receiver = SERVER_MICLOUD;
    ret=server_video_message(&msg);
    if(ret)  return -1;

	msg_init(&msg);
	msg.message = MSG_AUDIO_START;
	msg.sender = msg.receiver = SERVER_MICLOUD;
	ret=server_audio_message(&msg);
    if(ret)  return -1;
	 log_qcy(DEBUG_INFO, "start_stream_cmd end ok  ret=%d\n",ret);
	}
	else
	{
		msg_init(&msg);
		msg.message = MSG_VIDEO_STOP;
		msg.sender = msg.receiver = SERVER_MICLOUD;
		ret=server_video_message(&msg);
		if(ret)  return -1;

		msg_init(&msg);
		msg.message = MSG_AUDIO_STOP;
		msg.sender = msg.receiver = SERVER_MICLOUD;
		ret=server_audio_message(&msg);
		if(ret)  return -1;
		log_qcy(DEBUG_INFO, "exit_video_audio_stream_cmd end ok  ret=%\n",ret);
	}
 	return 0;
}


static int miio_initiative_reported()
{
	int ret = 0, i,id = 0;
	if(strlen(micloud_config.profile.did) <= 1 ) {
		message_t msg;
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MIIO_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_MICLOUD;
		msg.arg_in.cat = MIIO_PROPERTY_DID_STATUS;
		server_miio_message(&msg);
		/****************************/
		log_qcy(DEBUG_INFO, "------miio_initiative_reported----- strlen(micloud_config.profile.did) < 1------\n");
		return -1;
	}

    id =  misc_generate_random_id();
	 micloud_pro_info_t  temp_md_t={0};
	 cJSON *change_root  = cJSON_CreateObject();
	 cJSON *param_obj  = cJSON_CreateArray();

	cJSON_AddNumberToObject(change_root, "id", id);
	cJSON_AddStringToObject(change_root, "method", "properties_changed");
	cJSON_AddItemToObject(change_root, "params", param_obj);

	memcpy(&temp_md_t,&micloud_config.profile,sizeof(micloud_pro_info_t));
    //Housekeeping assistant is to receive attribute to report
	for(i = 1; i <=6; i++)
	{
		        cJSON *tmp0_obj  = cJSON_CreateObject();
		        cJSON_AddItemToArray(param_obj, tmp0_obj);
		        cJSON_AddStringToObject(tmp0_obj, "did",micloud_config.profile.did);
		        if(i<=5){
		        cJSON_AddNumberToObject(tmp0_obj, "piid", i);
		        cJSON_AddNumberToObject(tmp0_obj, "siid", 5);
		        }
		        if(i==1)  {
		        	bool track_switch;
		        	if(temp_md_t.motion_detection_switch)
						 track_switch = true;
					else
						 track_switch = false;
		        	cJSON_AddBoolToObject(tmp0_obj, "value",track_switch);
		        }
		        if(i==2)  {

		        	 cJSON_AddNumberToObject(tmp0_obj, "value", temp_md_t.alarm_interval);
		        }
		        if(i==3)  {

		        	 cJSON_AddNumberToObject(tmp0_obj, "value", temp_md_t.motion_sensitivity);
		        }
		        if(i==4)  {

					log_qcy(DEBUG_INFO, "--------reported StartTime:%s-----------------\n ",temp_md_t.motion_start);
		        	 cJSON_AddStringToObject(tmp0_obj, "value", temp_md_t.motion_start);
		        }
		        if(i==5)  {
					log_qcy(DEBUG_INFO, "--------reported endtime:%s-----------------\n ",temp_md_t.motion_end);
		        	 cJSON_AddStringToObject(tmp0_obj, "value", temp_md_t.motion_end);
		        }
		        if(i==6)  {
			        cJSON_AddNumberToObject(tmp0_obj, "piid", 9);
			        cJSON_AddNumberToObject(tmp0_obj, "siid", 6);
							bool report_switch;
							if(temp_md_t.cloud_upload_switch)
								report_switch = true;
							else
								report_switch = false;
							cJSON_AddBoolToObject(tmp0_obj, "value",report_switch);
		       		       }
	}

			char *ackbuf  = cJSON_Print(change_root);
	        if(ackbuf) {
	        	rpc_client_dispatch_msg(g_rpc_handle, ackbuf, strlen(ackbuf));
	        	    //log_qcy(DEBUG_INFO, "MIIO init  report changed, %s", ackbuf);
	        	    free(ackbuf);
	        }

			if(change_root) {
				cJSON_Delete(change_root);
			}
    	 return 0;
}

static void rpc_cb(void *data, uint32_t size, void *ptr)
{

	if(strstr((char *)data,"token mismatch"))
	{
		log_qcy(DEBUG_SERIOUS, "--DEBUG_SERIOUS--reply cb is: %s\n",  (char *)data);
		server_set_status(STATUS_TYPE_STATUS, STATUS_RESTART);
	}
    return;
}
/*
void rpc_cloud_switch_process(void *data, uint32_t size, void *ptr)
{
    printf("cloud switch process : %s\n", (char *)data);
}*/
int rpc_init()
{
    g_rpc_handle = rpc_client_init(128, rpc_cb);
    return 0;
}

static int alarm_config_update()
{
	int ret;
	g_alarm_cfg.alarm_switch = true;
	if(micloud_config.profile.alarm_interval <3)  micloud_config.profile.alarm_interval=3;
	g_alarm_cfg.alarm_interval = micloud_config.profile.alarm_interval -1;  //5:2

	sscanf(micloud_config.profile.motion_start, "%d:%d:%d", &g_alarm_cfg.alarm_start_hour, &g_alarm_cfg.alarm_start_min,&g_alarm_cfg.alarm_start_sec);
	sscanf(micloud_config.profile.motion_end, "%d:%d:%d",  &g_alarm_cfg.alarm_end_hour, &g_alarm_cfg.alarm_end_min,&g_alarm_cfg.alarm_end_sec);
	log_qcy(DEBUG_INFO, "upload stare-end time is %02d:%d%d-%02d:%d%d\n", g_alarm_cfg.alarm_start_hour, g_alarm_cfg.alarm_start_min ,g_alarm_cfg.alarm_start_sec,g_alarm_cfg.alarm_end_hour, g_alarm_cfg.alarm_end_min,g_alarm_cfg.alarm_end_sec);
	ret=mi_alarm_config_set(&g_alarm_cfg);
	return ret;
}

static int micloud_init()
{
	int ret,ret1=0;
    mi_cloud_info_t config;
    memset(&config, 0, sizeof(mi_cloud_info_t));
    config.video_stream = MEDIA_VIDEO_MAIN_CHN;
    config.audio_stream = MEDIA_AUDIO_ALAW_CHN;
    strncpy(config.save_path, "/tmp", strlen("/tmp"));
	config.default_configs = (mi_cloud_config_t*)malloc(sizeof(mi_cloud_config_t));
	if(micloud_config.profile.cloud_upload_switch)
		config.default_configs->cloud_switch=true;
	else
		config.default_configs->cloud_switch=false;

	if(micloud_config.profile.motion_detection_switch)
		config.default_configs->detection_switch=true;
	else
		config.default_configs->detection_switch=false;
	//config.default_configs->track_switch=true;
	if(micloud_config.profile.alarm_push)
		config.default_configs->pushSwitch=true;
	else
		config.default_configs->pushSwitch=false;
	/*
	log_info("config.default_configs->cloud_switch=%d\n",config.default_configs->cloud_switch);
	log_info("config.default_configs->detection_switch=%d\n",config.default_configs->detection_switch);
	log_info("config.default_configs->pushSwitch=%d\n",config.default_configs->pushSwitch);*/

    if(mi_cloud_init(&config, NULL) != MI_OK)
        return -1;

    ret=mi_cloud_set_upload_switch(1);
    ret1|=ret;
    rpc_init();
    set_log_params(stdout, micloud_config.profile.log_level,NULL, 1);
	log_qcy(DEBUG_INFO, "--micloud_init()--end--micloud  log_level=%d \n",micloud_config.profile.log_level);
    free(config.default_configs);
    return ret1;

}
static int stream_init(int quality)
{
    int ret;
    if(quality==0){
		log_qcy(DEBUG_INFO, " micloud quality ==0 ");
		stream_channel_info_t stream;
		stream.chn = MEDIA_VIDEO_MAIN_CHN;
		stream.video.encode = CODEC_VIDEO_H264;
		stream.fps = 15;
		stream.video.resolution = FLAG_RESOLUTION_VIDEO_360P;
		stream.bitrate = 1024; /*kbps*/
		stream.buffer_max_seconds = 0;
		ret = mi_ipc_stream_chn_add(&stream);
		if(ret < 0) {
			log_qcy(DEBUG_INFO, "add video stream error with %d\n", ret);
			return -1;
		}

		stream.chn = MEDIA_AUDIO_ALAW_CHN;
		stream.audio.samplebit = 16;
		stream.fps = 15;
		stream.audio.samplerate = 8000;
		stream.bitrate = 1024; /*kbps*/
		stream.buffer_max_seconds = 0;
		ret = mi_ipc_stream_chn_add(&stream);
		if(ret < 0) {
			log_qcy(DEBUG_INFO, "add audio stream error with %d\n", ret);
			return -1;
		}
    }

    if(quality==1){
		log_qcy(DEBUG_INFO, " micloud quality ==1 ");
		stream_channel_info_t stream;
		stream.chn = MEDIA_VIDEO_MAIN_CHN;
		stream.video.encode = CODEC_VIDEO_H264;
		stream.fps = 15;
		stream.video.resolution = FLAG_RESOLUTION_VIDEO_720P;
		stream.bitrate = 1024; /*kbps*/
		stream.buffer_max_seconds = 0;
		ret = mi_ipc_stream_chn_add(&stream);
		if(ret < 0) {
			log_qcy(DEBUG_INFO, "add video stream error with %d\n", ret);
			return -1;
		}

		stream.chn = MEDIA_AUDIO_ALAW_CHN;
		stream.audio.samplebit = 16;
		stream.fps = 15;
		stream.audio.samplerate = 8000;
		stream.bitrate = 1024; /*kbps*/
		stream.buffer_max_seconds = 0;
		ret = mi_ipc_stream_chn_add(&stream);
		if(ret < 0) {
			log_qcy(DEBUG_INFO, "add audio stream error with %d\n", ret);
			return -1;
		}
    }

    if(quality==2){
		log_qcy(DEBUG_INFO, " micloud quality ==2 ");
		stream_channel_info_t stream;
		stream.chn = MEDIA_VIDEO_MAIN_CHN;
		stream.video.encode = CODEC_VIDEO_H264;
		stream.fps = 15;
		stream.video.resolution = FLAG_RESOLUTION_VIDEO_1080P;
		stream.bitrate = 1024; /*kbps*/
		stream.buffer_max_seconds = 0;
		ret = mi_ipc_stream_chn_add(&stream);
		if(ret < 0) {
			log_qcy(DEBUG_INFO, "add video stream error with %d\n", ret);
			return -1;
		}

		stream.chn = MEDIA_AUDIO_ALAW_CHN;
		stream.audio.samplebit = 16;
		stream.fps = 15;
		stream.audio.samplerate = 8000;
		stream.bitrate = 1024; /*kbps*/
		stream.buffer_max_seconds = 0;
		ret = mi_ipc_stream_chn_add(&stream);
		if(ret < 0) {
			log_qcy(DEBUG_INFO, "add audio stream error with %d\n", ret);
			return -1;
		}
    }
	log_qcy(DEBUG_INFO, "stream_init()  end   --- \n");
    return 0;
}
static void str2hex(char *ds, unsigned char *bs, unsigned int n)
{
    int i;
    for (i = 0; i < n; i++)
        sprintf(ds + 2 * i, "%02x", bs[i]);
}
static int micloud_ipc_sdk_init()
{
    int ret = -1;
    mi_ipc_devinfo_t dev = {0};

    str2hex(dev.device_token, micloud_config.profile.token, strlen(micloud_config.profile.token));
    strncpy(dev.model, micloud_config.profile.token, sizeof(micloud_config.profile.token));
	log_qcy(DEBUG_INFO, "----micloud--dev.device_token=%s------\n",dev.device_token);

   // strncpy(dev.did, "1020003077", sizeof(dev.did));
    ret = mi_ipc_sdk_init(&dev, NULL);
    if(ret < 0) {
    	log_qcy(DEBUG_SERIOUS, "ipc sdk init error with %d\n", ret);
        return -1;
    }
    return ret;
}

static int server_release(void)
{
	int ret = 0;
	mi_ipc_sdk_deinit();
	mi_cloud_deinit();
	return ret;
}
static int server_get_status(int type)
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
    	log_qcy(DEBUG_SERIOUS, "add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		st = info.status;
	else if(type== STATUS_TYPE_EXIT)
		st = info.exit;
	else if(type==STATUS_TYPE_CONFIG)
		st = micloud_config.status;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
	log_qcy(DEBUG_SERIOUS, "add unlock fail, ret = %d", ret);
	return st;
}
static int server_set_status(int type, int st)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
    	log_qcy(DEBUG_SERIOUS, "add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		micloud_config.status = st;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_qcy(DEBUG_SERIOUS, "add unlock fail, ret = %d", ret);
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0;
	message_t msg;
	message_t send_msg;
	msg_init(&msg);
	msg_init(&send_msg);
	pthread_mutex_lock(&m_mutex);
	if( message.head == message.tail ) {
		if( info.status==STATUS_RUN   ) {
			pthread_cond_wait(&m_cond,&m_mutex);
		}
	}

	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&m_mutex);
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1) {
		return 0;
	}
	switch(msg.message){
		case MSG_MANAGER_EXIT:
				server_set_status(STATUS_TYPE_EXIT,1);
				main_thread_exit_termination(2);
				video_audio_cmd(0);
				//misc_set_bit(&info.init_status, M_HANG_UP_FLAG, 1);
				misc_set_bit(&info.init_status, PUT_FRAME_PAUE_FLAG, 1);
			    /********message body********/
				message_t msg2;
				msg_init(&msg2);
				msg2.message = MSG_MANAGER_EXIT_ACK;
				msg2.sender = SERVER_MICLOUD;
				manager_message(&msg2);
				/***************************/
				log_qcy(DEBUG_INFO,"-----------thread exit: server_micloud----pause-------");
			break;
		case MSG_MANAGER_TIMER_ACK:
			log_qcy(DEBUG_INFO, "----------into -micloud-proc- MSG_MANAGER_TIMER_ACK  ----\n ");
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			log_qcy(DEBUG_INFO, "into  PRO  MSG_MIIO_PROPERTY_GET_ACK  from server miio\n");
			log_qcy(DEBUG_INFO, " msg.arg_in.cat =%d  msg.arg_in.dog =%d \n", msg.arg_in.cat,msg.arg_in.dog);
			if( msg.arg_in.cat == MIIO_PROPERTY_CLIENT_STATUS ) {
					if(msg.arg_in.dog == STATE_CLOUD_CONNECTED)
						{
						if(info.status ==STATUS_NONE)
							{
								server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
							}
						else if(info.status ==STATUS_IDLE)
							{
								server_set_status(STATUS_TYPE_STATUS, STATUS_RESTART);
							}
						}
			}
			else if( msg.arg_in.cat == MIIO_PROPERTY_DID_STATUS ) {
					if( msg.arg_in.dog == 1 ){
						if(info.status ==STATUS_START || info.status ==STATUS_NONE)
						{
							if(msg.arg==NULL) break;
							memcpy(micloud_config.profile.did,(char*)(msg.arg),MAX_SYSTEM_STRING_SIZE);
							log_qcy(DEBUG_INFO," micloud_config.profile.did =%s \n",micloud_config.profile.did);
						}

					}
				}
			break;
		case MSG_VIDEO_START_ACK:
			log_info("into  MSG_VIDEO_START_ACK\n");
			break;
		case MSG_AUDIO_START_ACK:
			log_info("into  MSG_AUDIO_START_ACK\n");
			break;

		case MICLOUD_EVENT_TYPE_PEOPLEMOTION:
			//有人移动
			log_qcy(DEBUG_INFO, "into  PRO MICLOUD_EVENT_TYPE_PEOPLEMOTION\n");
			if(info.status!=STATUS_RUN)  break;
			if(time_count <= 170  &&  (misc_get_bit( info.init_status, PUT_FRAME_PAUE_FLAG )==0) )  break;
			if( misc_get_bit( info.init_status, PUT_FRAME_PAUE_FLAG ) )
			{
				main_thread_exit_termination(0);
				ret=video_audio_cmd(1);
				if(ret){
				log_qcy(DEBUG_INFO," video_audio_cmd error  break");
				break;
				}
				log_qcy(DEBUG_INFO, "into MICLOUD_EVENT_TYPE_PEOPLEMOTION  misc_get_bit( info.init_status, PUT_FRAME_PAUE_FLAG  \n");
				misc_set_bit(&info.init_status, PUT_FRAME_PAUE_FLAG, 0);
				micloud_put_frame_puse_timer();
			}
			usleep(500000);
			ret=mi_cloud_upload(CLOUD_EVENT_TYPE_PEOPLEMOTION);
			if(ret)
				log_qcy(DEBUG_INFO, "upload PEOPLEMOTION event faile /n");
			time_count=0;
			break;
		case MICLOUD_EVENT_TYPE_OBJECTMOTION:
			//画面变动
			log_qcy(DEBUG_INFO, "into  PRO MICLOUD_EVENT_TYPE_OBJECTMOTION\n");
			if(info.status!=STATUS_RUN)  break;
			if(time_count <= 170  &&  (misc_get_bit( info.init_status, PUT_FRAME_PAUE_FLAG )==0) )  break;
			if( misc_get_bit( info.init_status, PUT_FRAME_PAUE_FLAG ) )
			{
				main_thread_exit_termination(0);
				ret=video_audio_cmd(1);
				if(ret){
				log_qcy(DEBUG_INFO," video_audio_cmd error  break");
				break;
				}
				log_qcy(DEBUG_INFO, "into MICLOUD_EVENT_TYPE_OBJECTMOTION  misc_get_bit( info.init_status, PUT_FRAME_PAUE_FLAG  \n");
				misc_set_bit(&info.init_status, PUT_FRAME_PAUE_FLAG, 0);
				micloud_put_frame_puse_timer();
			}
			usleep(500000);
			ret=mi_cloud_upload(CLOUD_EVENT_TYPE_OBJECTMOTION);
			if(ret)
			log_qcy(DEBUG_INFO, "upload OBJECTMOTION event faile /n");
			time_count=0;
			break;
		case MSG_MICLOUD_CHANGE_PARA:
			log_info("----------into --proc- MSG_MICLOUD_CHANGE_PARA  ----\n ");
			if( msg.arg_in.cat == MICLOUD_CTRL_MOTION_SWITCH ) {

				int temp = *((int*)(msg.arg));
				if( temp == micloud_config.profile.motion_detection_switch) {
					break;
					}
				micloud_config.profile.motion_detection_switch=temp;
				log_qcy(DEBUG_INFO, "upload micloud_config.profile.motion_detection_switch =%d\n",	micloud_config.profile.motion_detection_switch);
			}
			else if( msg.arg_in.cat == MICLOUD_CTRL_MOTION_ALARM_INTERVAL) {

					int temp = *((int*)(msg.arg));
					if( temp == micloud_config.profile.alarm_interval ) {
						break;
						}
					if(temp == 0)  break;
					micloud_config.profile.alarm_interval=temp;
					log_qcy(DEBUG_INFO, "upload 	micloud_config.profile.alarm_interval =%d\n",	micloud_config.profile.alarm_interval);
				}
			else if( msg.arg_in.cat == MICLOUD_CTRL_MOTION_SENSITIVITY) {
					int temp = *((int*)(msg.arg));
					if( temp == micloud_config.profile.motion_sensitivity ) {
						break;
					}
					micloud_config.profile.motion_sensitivity = temp;
					log_qcy(DEBUG_INFO, "upload micloud_config.profile.motion_sensitivity =%d\n",micloud_config.profile.motion_sensitivity);
				}
			else if( msg.arg_in.cat == MICLOUD_CTRL_CUSTOM_WARNING_PUSH) {
					int temp = *((int*)(msg.arg));
					if( temp == micloud_config.profile.alarm_push ) {
						break;
					}
					micloud_config.profile.alarm_push = temp;
					log_qcy(DEBUG_INFO, "upload micloud_config.profile.alarm_push =%d\n",micloud_config.profile.alarm_push);

				}
			else if( msg.arg_in.cat == MICLOUD_CTRL_MOTION_START) {
					char *temp = (char*)(msg.arg);
					if( !strcmp( temp, micloud_config.profile.motion_start) ) {
						break;
					}
					strncpy( micloud_config.profile.motion_start, temp,strlen(temp));
					log_qcy(DEBUG_INFO, "upload  micloud_config.profile.motion_start =%s\n",micloud_config.profile.motion_start);
				}
			else if( msg.arg_in.cat == MICLOUD_CTRL_MOTION_END) {
					char *temp = (char*)(msg.arg);
					if( !strcmp( temp,  micloud_config.profile.motion_end) ) {
						break;
					}
					strncpy( micloud_config.profile.motion_end, temp,strlen(temp));
					log_qcy(DEBUG_INFO, "upload  micloud_config.profile.motion_end =%s\n",micloud_config.profile.motion_end);

				}
			/*else if( msg.arg_in.cat == MICLOUD_CTRL_CUSTOM_MICLOUD_UPLOAD_SWITCH) {
					int *temp = *((int*)(msg.arg));
					if( temp == micloud_config.profile.cloud_upload_switch) {
						break;
					}
					micloud_config.profile.cloud_upload_switch=temp;
					log_info("upload  micloud_config.profile.cloud_upload_switch =%d\n",micloud_config.profile.cloud_upload_switch);

				}*/
				alarm_config_update();
				config_micloud_set(CONFIG_MICLOUD_PROFILE,&micloud_config.profile);
				break;
		default:
			log_err("not processed message = %d", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick) > 10 ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_MICLOUD;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		ret = manager_message(&msg);
		/***************************/
	}
	return ret;
}

static void task_default(void)
{
	int ret;
	message_t msg;
	switch( info.status ){
		case STATUS_NONE:
		    /********message body********/
			msg_init(&msg);
			msg.message = MSG_MIIO_PROPERTY_GET;
			msg.sender = msg.receiver = SERVER_MICLOUD;
			msg.arg_in.cat = MIIO_PROPERTY_CLIENT_STATUS;
			server_miio_message(&msg);
			/****************************/
			sleep(2);
			log_qcy(DEBUG_INFO,"micloud STATUS_NONE\n");
			break;
		case STATUS_WAIT:
			log_qcy(DEBUG_INFO,"STATUS_WAIT\n");
			memset(&micloud_config, 0, sizeof(micloud_config_t));
			ret = config_micloud_read(&micloud_config);
//			log_qcy(DEBUG_INFO,"micloud_config.profile.model=%s\n",micloud_config.profile.model);
//			log_qcy(DEBUG_INFO,"micloud_config.profile.token=%s\n",micloud_config.profile.token);
//			log_qcy(DEBUG_INFO,"enable=%d\n",micloud_config.profile.motion_detection_switch);
//			log_qcy(DEBUG_INFO,"cloud_report=%d\n",micloud_config.profile.alarm_push);
//			log_qcy(DEBUG_INFO,"alarm_interval=%d\n",micloud_config.profile.alarm_interval);
//			log_qcy(DEBUG_INFO,"sensitivity=%d\n",micloud_config.profile.motion_sensitivity);
//			log_qcy(DEBUG_INFO,"start=%s\n",micloud_config.profile.motion_start);
//			log_qcy(DEBUG_INFO,"end=%s\n",micloud_config.profile.motion_end);
//			log_qcy(DEBUG_INFO,"cloud_switch=%d\n",micloud_config.profile.cloud_upload_switch);
//			log_qcy(DEBUG_INFO,"quality=%d\n",micloud_config.profile.quality);
			if( ret == 0 )
				server_set_status(STATUS_TYPE_STATUS, STATUS_SETUP);
			else
				sleep(2);
			break;
		case STATUS_SETUP:
			log_qcy(DEBUG_INFO,"micloud STATUS_SETUP\n");
		    if(micloud_ipc_sdk_init() != 0 ){
				log_qcy(DEBUG_SERIOUS, "micloud_ipc_sdk_init error");
		        server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
				break;
		    }
		    if (stream_init(micloud_config.profile.quality) < 0) {
				log_qcy(DEBUG_SERIOUS, "stream_init error");
		        server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
				break;
		    }
		    if(micloud_init()  < 0)
		    {
		        server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
				log_qcy(DEBUG_SERIOUS,"micloud_init error");
				break;

		    }
			sleep(2);
			if(alarm_config_update() !=0 )
		    {
		        server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
				log_qcy(DEBUG_SERIOUS,"alarm_config_update error");
				break;

		    }
		    server_set_status(STATUS_TYPE_STATUS, STATUS_START);
			log_qcy(DEBUG_INFO,"--intoSTATUS_SETUP end--\n");
			break;
		case STATUS_IDLE:
			//if(hang_up_flag == 1)
			//server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			sleep(1);
			break;
		case STATUS_START:
			ret=miio_initiative_reported();
			if(ret)  {
				server_set_status(STATUS_TYPE_STATUS, STATUS_START);
				log_qcy(DEBUG_INFO,"micloud miio_initiative_reported failed\n");
				sleep(2);
				break;
			}
			misc_set_bit(&info.init_status, PUT_FRAME_PAUE_FLAG, 1);
			main_thread_exit_termination(0);
			ret=video_audio_cmd(1);
			ret=creat_video_thread();
			ret=creat_audio_thread();
			if(ret){
			log_qcy(DEBUG_INFO," video_audio_cmd error  break");
			sleep(2);
			break;
			}
			server_set_status(STATUS_TYPE_STATUS, STATUS_RUN);
			log_qcy(DEBUG_INFO,"create micloud server finished------");
			break;
		case STATUS_RUN:
		    //log_info("micloud ruing\n");
			break;
		case STATUS_STOP:
			break;
		case STATUS_RESTART:
			server_release();
			sleep(2);
			server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			log_qcy(DEBUG_SERIOUS, "server micloud STATUS_RESTART\n");
			break;
		case STATUS_ERROR:
			server_release();
			sleep(2);
			server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			log_qcy(DEBUG_SERIOUS, "server micloud STATUS_ERROR\n");
			break;
	}
	usleep(50000);
	return;
}


/*
 * server entry point
 */
static void server_thread_termination(void)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_MICLOUD_SIGINT;
	msg.sender = msg.receiver = SERVER_MICLOUD;
	manager_message(&msg);
	/****************************/
}
static void *server_func(void *arg)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_micloud");
	pthread_detach(pthread_self());
	if( !message.init ) {
		msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	}
	memset(&info, 0, sizeof(server_info_t));
	info.status=STATUS_NONE;
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while(!info.exit) {
		info.task.func();
		server_message_proc();
	}
	msg_buffer_release(&message);
	server_release();
	//log_qcy(DEBUG_INFO,"-----------thread exit: server_micloud-----------");
	pthread_exit(0);
}


//*
// * external interface
// */
int server_micloud_start(void)
{
	int ret=-1;
//	if( misc_get_bit( info.init_status, M_HANG_UP_FLAG ) ) {
//		server_set_status(STATUS_TYPE_STATUS, STATUS_START);
//		server_set_status(STATUS_TYPE_EXIT,0);
//		log_qcy(DEBUG_INFO,"micloud server_func pthread_create successful!");
//		pthread_cond_signal(&m_cond);
//		return 0;
//	}
	pthread_rwlock_init(&info.lock, NULL);
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "micloud server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_qcy(DEBUG_INFO,"micloud server_func pthread_create successful!");
		return 0;
	}
}

int server_micloud_message(message_t *msg)
{
	int ret=0,ret1;
	/*if( server_get_status(STATUS_TYPE_STATUS)!= STATUS_RUN ) {
		log_err("micloud server is not ready!");
		return -1;
	}*/
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "micloud server is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		pthread_rwlock_unlock(&message.lock);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	if( ret!=0 )
		log_err("message push in micloud_server error =%d", ret);
	else {
		pthread_cond_signal(&m_cond);
	}
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}

