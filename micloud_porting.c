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
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "micloud.h"

static message_buffer_t		video_buff;
static message_buffer_t		audio_buff;
static pthread_rwlock_t		lock1=PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t		lock2=PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t		alock=PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t		vlock=PTHREAD_RWLOCK_INITIALIZER;

static	pthread_mutex_t			vmutex=PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t			vcond = PTHREAD_COND_INITIALIZER;
static	pthread_mutex_t			amutex=PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t			acond = PTHREAD_COND_INITIALIZER;

static FILE *fp_img = NULL;
static int pthread_exit_flags=0;
static int a_pthread_creat_flags=0;
static int v_pthread_creat_flags=0;

static int local_send_video_frame(av_packet_t *packet);
static int local_send_audio_frame(av_packet_t *packet);
static int sendto_video_exit(void);
static int sendto_audio_exit(void);



static int sendto_video_exit(void)
{
		int ret;
		message_t msg;
	    /********message video body********/
		msg_init(&msg);
		msg.message = MSG_VIDEO_STOP;
		msg.sender = msg.receiver = SERVER_MICLOUD;
	    ret=server_video_message(&msg);
		/****************************/
	    if(ret)  return -1;
		log_qcy(DEBUG_INFO, "get_video_stream_cmd end ok  ret=%\n",ret);
		return ret;
}
static int sendto_audio_exit(void)
{
		int ret;
		message_t msg;
	    /********message audio  body********/
		msg_init(&msg);
		msg.message = MSG_AUDIO_STOP;
		msg.sender = msg.receiver = SERVER_MICLOUD;
		ret=server_audio_message(&msg);
		/****************************/
	    if(ret)  return -1;
		log_qcy(DEBUG_INFO, "get_audio_stream_cmd end ok  ret=%\n",ret);
	return ret;
}

static int local_send_video_frame(av_packet_t *packet)
{
    mi_frame_info_t pvinfo = {0};
    int ret;
    pthread_rwlock_rdlock(&vlock);
    if( (*(packet->init) == 0 ) || packet->data == NULL  )
    {
    	log_qcy(DEBUG_INFO, "packet->data == NULL\n");
    	pthread_rwlock_unlock(&vlock);
    	return -1;
    }
   // pvinfo=( mi_frame_info_t *)malloc(sizeof(mi_frame_info_t));
    pvinfo.data = (unsigned char*)(packet->data);
    pvinfo.timestamp = packet->info.timestamp;
    pvinfo.timestamp_s = packet->info.timestamp/1000;
    pvinfo.seqNo = packet->info.index;

    pvinfo.data_size = packet->info.size;
    if( misc_get_bit(packet->info.flag, 0/*RTSTREAM_PKT_FLAG_KEY*/) )// I frame
    { pvinfo.type = MEDIA_FRAME_VIDEO_I;
   // log_qcy(DEBUG_SERIOUS,"key frame -->MEDIA_FRAME_VIDEO_I\n");
    }
      else{
    	  pvinfo.type = MEDIA_FRAME_VIDEO_PB;
         // log_qcy(DEBUG_SERIOUS,"key frame -->MEDIA_FRAME_VIDEO_p\n");
      }

    ret = mi_ipc_stream_put_frame(MEDIA_VIDEO_MAIN_CHN, &pvinfo);
    if(ret != 0 ) {
        log_err("ringbuffer put video frame error %d\n", ret);
    }

	av_packet_sub(packet);
	pthread_rwlock_unlock(&vlock);

    return ret;
}

static int local_send_audio_frame(av_packet_t *packet)
{
    mi_frame_info_t pvinfo = {0};
    int ret;
    pthread_rwlock_rdlock(&alock);
    if( (*(packet->init) == 0 )|| packet->data == NULL  )
    {
    	pthread_rwlock_unlock(&alock);
    	return -1;
    }
    //pvinfo=( mi_frame_info_t *)malloc(sizeof(mi_frame_info_t));

    pvinfo.data = (unsigned char*)(packet->data);
    pvinfo.timestamp = packet->info.timestamp;
    pvinfo.timestamp_s = packet->info.timestamp/1000;
    pvinfo.seqNo = packet->info.index;
    pvinfo.data_size = packet->info.size;

    ret = mi_ipc_stream_put_frame(MEDIA_AUDIO_ALAW_CHN, &pvinfo);
    //log_err(" mi_ipc_stream_put_frame  audio \n");
    if(ret != 0 ) {
        log_err("ringbuffer put audio frame error %d\n", ret);
    }
	av_packet_sub(packet);
	pthread_rwlock_unlock(&alock);
    return ret;

}

static void *local_video_send_thread(void *arg)
{
    printf("enter thread local_video_send_thread send\n");
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
	misc_set_thread_name("local_video_send_thread");
    message_t	msg;
    v_pthread_creat_flags=1;
	if( !video_buff.init ) {
		msg_buffer_init(&video_buff, MSG_BUFFER_OVERFLOW_YES);
	}
    int ret,ret1;
    while(!pthread_exit_flags) {
    	   //read video frame
    	//condition
    	pthread_mutex_lock(&vmutex);
    	if( video_buff.head == video_buff.tail ) {
          //  log_qcy(DEBUG_SERIOUS, "--pthread_cond_wait video-------");
			pthread_cond_wait(&vcond, &vmutex);
    	}
			msg_init(&msg);
			ret = msg_buffer_pop(&video_buff, &msg);
	    	pthread_mutex_unlock(&vmutex);
	    	if( ret!=0 ){
	         //   log_qcy(DEBUG_SERIOUS, "--continue video-------");
	    		continue;
	    	}
	        if(ret == 0) {
	            local_send_video_frame((av_packet_t*)(msg.arg));
	        }
	        msg_free(&msg);
    }

    sendto_video_exit();
    msg_buffer_release2(&video_buff, &vmutex);
    v_pthread_creat_flags=0;
    log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_micloud_video_stream----------");
    pthread_exit(0);
}

static void *local_audio_send_thread(void *arg)
{
    printf("enter thread audio send\n");
    message_t	msg;
    int ret,ret1;
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
	misc_set_thread_name("local_audio_send_thread");
    a_pthread_creat_flags=1;
	if( !audio_buff.init ) {
		msg_buffer_init(&audio_buff, MSG_BUFFER_OVERFLOW_YES);
	}
    while(!pthread_exit_flags) {
    	   //read audio frame
    	//condition
    	pthread_mutex_lock(&amutex);
    	if( audio_buff.head == audio_buff.tail ) {
			pthread_cond_wait(&acond, &amutex);
    	}

			msg_init(&msg);
			ret = msg_buffer_pop(&audio_buff, &msg);
	    	pthread_mutex_unlock(&amutex);
	    	if( ret!=0 )
	    		continue;
	        if(ret == 0) {
	            local_send_audio_frame((av_packet_t*)(msg.arg));
	        }
	        msg_free(&msg);
    }
    sendto_audio_exit();
    msg_buffer_release2(&audio_buff, &amutex);
    a_pthread_creat_flags=0;
    log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_micloud_audio_stream----------");
    pthread_exit(0);
}

/*
 * interface
 */

int creat_video_thread()
{
	int ret;
	pthread_t video_pid;
	if(v_pthread_creat_flags)  {
		log_qcy(DEBUG_INFO, "v_pthread_creat_flags  is 1!");
		return 0;
	}
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
	if(a_pthread_creat_flags)  {
		log_qcy(DEBUG_INFO, "a_pthread_creat_flags  is 1!");
		return 0;
	}
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
	log_qcy(DEBUG_INFO, "mi_cloud_on_error");
    return 0;
}

int mi_cloud_get_snapshot(int pic_id)
{
		// need to modification

	log_qcy(DEBUG_INFO,"this is mi_cloud_get_snapshot pic_id is %d\n",pic_id);
    fp_img = fopen(MOTION_PICTURE_NAME, "rb");
    if((fp_img == NULL)) {
        log_info();
    	log_qcy(DEBUG_INFO, "can't read live picture file motion.jpeg\n");
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
	int ret=0,id = -1;
	//id = msg->arg_in.wolf;
    pthread_rwlock_rdlock(&lock1);
	if( !video_buff.init ) {
		log_qcy(DEBUG_INFO, "micloud video is not ready for message processing!");
		pthread_rwlock_unlock(&lock1);
		return -1;
	}
	ret = msg_buffer_push(&video_buff, msg);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in micloud error =%d", ret);
	else {
		pthread_cond_signal(&vcond);
	}
	pthread_rwlock_unlock(&lock1);
	return ret;
}

int server_micloud_audio_message(message_t *msg)
{
	int ret=0,id = -1;
	//id = msg->arg_in.wolf;
    pthread_rwlock_rdlock(&lock2);
	if( !audio_buff.init ) {
		log_qcy(DEBUG_INFO, "micloud audio is not ready for message processing!");
		pthread_rwlock_unlock(&lock2);
		return -1;
	}
	ret = msg_buffer_push(&audio_buff, msg);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in micloud error =%d", ret);
	else {
		pthread_cond_signal(&acond);
	}

	pthread_rwlock_unlock(&lock2);
	return ret;
}
void main_thread_exit_termination(int arg)
{
	log_qcy(DEBUG_INFO, "micloud miclou_porting_exit_termination! arg =%d\n",arg);
	pthread_exit_flags=arg;
	pthread_cond_signal(&acond);
	pthread_cond_signal(&vcond);
}
