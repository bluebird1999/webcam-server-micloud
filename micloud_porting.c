/*
 * micloud_porting.c
 *
 *  Created on: 2020年10月25日
 *      Author: kang
 */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <miot_time.h>
#include <mi_ipc_api.h>
#include <mi_cloud_api.h>
#include <rpc_client.h>
#include <mi_cloud_porting.h>
#include "../../tools/tools_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "micloud.h"

static message_buffer_t		video_buff;
static message_buffer_t		audio_buff;
static pthread_rwlock_t			lock;
static FILE *fp_img = NULL;
static int pthread_exit_flags=0;
static int a_pthread_creat_flags=0;
static int v_pthread_creat_flags=0;

static int local_send_video_frame(message_t *msg);
static int local_send_audio_frame(message_t *msg);


static int local_send_video_frame(message_t *msg)
{
    mi_frame_info_t *pvinfo = NULL;
    int ret;
    int flag;
    av_data_info_t	*avinfo;
    pvinfo=( mi_frame_info_t *)malloc(sizeof(mi_frame_info_t));
    pvinfo->data = (unsigned char*)(msg->extra);
    if(  pvinfo->data==NULL || msg->arg==NULL ) {
    	log_err("--pvinfo->data==NULL || msg->arg==NULL--\n");
    	return -1;
    }
    avinfo = (av_data_info_t*)(msg->arg);
    pvinfo->timestamp = avinfo->timestamp;
    pvinfo->timestamp_s = avinfo->timestamp/1000;
    pvinfo->seqNo = avinfo->frame_index;
    pvinfo->data_size = msg->extra_size;
    flag = pvinfo->data[4];
    if( flag != 0x41 ) {	// I frame
        pvinfo->type = MEDIA_FRAME_VIDEO_I;
   //     log_info("key frame -->MEDIA_FRAME_VIDEO_I\n");
    } else {
        pvinfo->type = MEDIA_FRAME_VIDEO_PB;
    }

    ret = pthread_rwlock_wrlock(&lock);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "add session wrlock fail, ret = %d", ret);

	}
    ret = mi_ipc_stream_put_frame(MEDIA_VIDEO_MAIN_CHN, pvinfo);
    if(ret != 0 ) {
        log_err("ringbuffer put video frame error %d\n", ret);
    }
    ret = pthread_rwlock_unlock(&lock);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "add session unlock fail, ret = %d", ret);
		return -1;
	}
	free(pvinfo);
    return ret;
}

static int local_send_audio_frame(message_t *msg)
{
    mi_frame_info_t *pvinfo = NULL;
    int ret;
    av_data_info_t	*avinfo;
    pvinfo=( mi_frame_info_t *)malloc(sizeof(mi_frame_info_t));
    pvinfo->data = (unsigned char*)(msg->extra);
    if(  pvinfo->data==NULL || msg->arg==NULL )
    	{
    	log_err("--pvinfo->data==NULL || msg->arg==NULL--\n");
    	return -1;}
    avinfo = (av_data_info_t*)(msg->arg);
    pvinfo->timestamp = avinfo->timestamp;
    pvinfo->timestamp_s = avinfo->timestamp/1000;
    pvinfo->seqNo = avinfo->frame_index;
    pvinfo->data_size = msg->extra_size;
    ret = pthread_rwlock_wrlock(&lock);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "add session wrlock fail, ret = %d", ret);

	}

    ret = mi_ipc_stream_put_frame(MEDIA_AUDIO_ALAW_CHN, pvinfo);
    //log_err(" mi_ipc_stream_put_frame  audio \n");
    if(ret != 0 ) {
        log_err("ringbuffer put audio frame error %d\n", ret);
    }
    ret = pthread_rwlock_unlock(&lock);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "add session unlock fail, ret = %d", ret);
		return -1;
	}
	free(pvinfo);
    return ret;

}

static void *local_video_send_thread(void *arg)
{
    printf("enter thread local_video_send_thread send\n");
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
    message_t	msg;
    v_pthread_creat_flags=1;
	if( !video_buff.init ) {
		msg_buffer_init(&video_buff, MSG_BUFFER_OVERFLOW_YES);
	}
    int ret,ret1;
    while(!pthread_exit_flags) {
    	   //read video frame
			ret = pthread_rwlock_wrlock(&video_buff.lock);
			if(ret)	{
				log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d", ret);
				continue;
			}
			msg_init(&msg);
			ret = msg_buffer_pop(&video_buff, &msg);
			ret1 = pthread_rwlock_unlock(&video_buff.lock);
			if (ret1) {
				log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d", ret1);
				msg_free(&msg);
				continue;
			}

	    	if( ret!=0 )
	    		continue;
	        if(ret == 0) {
	            local_send_video_frame(&msg);
	        }
	        else {
	            usleep(1000);
	            continue;
	        }
	        msg_free(&msg);
           // usleep(40000);
	       // usleep(3000);
    }
    pthread_exit_flags=0;
    v_pthread_creat_flags=0;
    log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_micloud_video_stream----------");
    msg_buffer_release(&video_buff);
    pthread_exit(0);
}

static void *local_audio_send_thread(void *arg)
{
    printf("enter thread audio send\n");
    message_t	msg;
    int ret,ret1;
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
    a_pthread_creat_flags=1;
	if( !audio_buff.init ) {
		msg_buffer_init(&audio_buff, MSG_BUFFER_OVERFLOW_YES);
	}
    while(!pthread_exit_flags) {
    	   //read audio frame
			ret = pthread_rwlock_wrlock(&audio_buff.lock);
			if(ret)	{
				log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d", ret);
				continue;
			}
			msg_init(&msg);
			ret = msg_buffer_pop(&audio_buff, &msg);
			ret1 = pthread_rwlock_unlock(&audio_buff.lock);
			if (ret1) {
				log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d", ret1);
				msg_free(&msg);
				continue;
			}

	    	if( ret!=0 )
	    		continue;
	        if(ret == 0) {
	            local_send_audio_frame(&msg);
	        }
	        else {
	            usleep(1000);
	            continue;
	        }
	        msg_free(&msg);
            //usleep(40000);
	        //usleep(3000);
    }
    pthread_exit_flags=0;
    a_pthread_creat_flags=0;
    log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_micloud_audio_stream----------");
    msg_buffer_release(&audio_buff);
    pthread_exit(0);
}

/*
 * interface
 */

int creat_video_thread()
{
	int ret;
	pthread_t video_pid;
	if(v_pthread_creat_flags)  return 0;
	pthread_rwlock_init(&lock, NULL);
	ret = pthread_create(&video_pid, NULL, local_video_send_thread, NULL);
	if(ret < 0) {
		printf("pthread create error with %s\n", strerror(errno));
		return -1;
	}
	log_qcy(DEBUG_INFO, "micloud creat_video_thread sucees ret1=%\n",ret);
	return ret;
}


int creat_audio_thread()
{
	int ret;
	pthread_t audio_pid;
	if(a_pthread_creat_flags)  return 0;
	ret = pthread_create(&audio_pid, NULL, local_audio_send_thread, NULL);
	if(ret < 0) {
		printf("pthread create error with %s\n", strerror(errno));
		return -1;
	}
	log_qcy(DEBUG_INFO, "micloud creat_audio_thread sucees ret1=%\n");
	return ret;
}

int mi_cloud_rpc_send(void *rpc_id, const char *method, const char *params)
{
    return 0;
}

int mi_cloud_on_error(int ErrorCode)
{
    return 0;
}

int mi_cloud_get_snapshot(int pic_id)
{
		// need to modification
	//char fname[MAX_SYSTEM_STRING_SIZE*2];
	//log_qcy(DEBUG_INFO,"this is mi_cloud_get_snapshot pic_id is %d\n",pic_id);

	//memset(fname,0,sizeof(fname));
	//snprintf(fname,MAX_SYSTEM_STRING_SIZE*2,"%s%s",_config_.qcy_path, MOTION_PICTURE_PATH);
    fp_img = fopen("./resource/motion.jpeg", "rb");
    if((fp_img == NULL)) {
        log_info();
    	log_qcy(DEBUG_INFO, "can't read live picture file ./resource/motion.jpeg\n");
        return -1;
    }
    if(fp_img) {
        fseek(fp_img, 0, SEEK_END);
        int size = ftell(fp_img);
        char *data = (char *)malloc(size);
        fseek(fp_img, 0, SEEK_SET);
        fread(data, 1, size, fp_img);

        mi_cloud_set_snapshot(pic_id, data, size);
        free(data);
    }
    return 0;
}


int mi_cloud_force_key_frame(int stream_id)
{
    return 0;
}

int server_micloud_video_message(message_t *msg)
{
	int ret=0,ret1;
	if( !video_buff.init ) {
		log_qcy(DEBUG_INFO, "micloud video is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&video_buff.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d", ret);
		return ret;
	}
	ret = msg_buffer_push(&video_buff, msg);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in micloud error =%d", ret);
	ret1 = pthread_rwlock_unlock(&video_buff.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d", ret1);
	return ret;
}

int server_micloud_audio_message(message_t *msg)
{
	int ret=0,ret1=0;
	if( !audio_buff.init ) {
		log_qcy(DEBUG_INFO, "micloud audio is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&audio_buff.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d", ret);
		return ret;
	}
	ret = msg_buffer_push(&audio_buff, msg);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in micloud error =%d", ret);
	ret1 = pthread_rwlock_unlock(&audio_buff.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d", ret1);
	return ret;
}
void main_thread_exit_termination()
{
	pthread_exit_flags=1;
}
