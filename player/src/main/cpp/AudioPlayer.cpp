//
// Created by king on 18-7-18.
//

#include "AudioPlayer.h"
#include "AndroidLog.h"


void AudioPlayer::start() {

}

static void* toPrepared(void* data){
    AudioPlayer *player = (AudioPlayer*)data;
    player->prepared_fun();
    pthread_detach(pthread_self());
    pthread_exit(&player->thread_t);
}

void AudioPlayer::setPrepared() {
    pthread_create(&this->thread_t, NULL, toPrepared, this);
}

void AudioPlayer::setSource(const char* path) {
    int len = strlen(path);
    this->path = (char*)malloc(len + 1);
    strcpy(this->path, path);
    LOGD("set source path %s\n", this->path);
}

AudioPlayer::AudioPlayer(JavaVM *g_javaVM) {
    this->g_javaVM = g_javaVM;
}

AudioPlayer::~AudioPlayer() {
    free (this->path);
    JNIEnv *env;
    this->g_javaVM->AttachCurrentThread(&env, NULL);
    if(this->jmidMetadata)
        env->DeleteGlobalRef(this->objMetaData);

    if(this->jmidOnError)
        env->DeleteGlobalRef(this->objOnError);

    if(this->jmidOnparpared)
        env->DeleteGlobalRef(this->objOnPrepared);

    if(this->jmidBaseInfo)
        env->DeleteGlobalRef(this->objBaseInfo);

    this->g_javaVM->DetachCurrentThread();
}


void AudioPlayer::setBaseInfoListener(jmethodID listener, jobject obj) {
    LOGD("jni set on base info listener.\n");
    this->jmidBaseInfo = listener;
    this->objBaseInfo = obj;
}


void AudioPlayer::setOnErrorListener(jmethodID listener, jobject obj) {
    LOGD("jni set on error listener.\n");
    this->jmidOnError = listener;
    this->objOnError = obj;
}

void AudioPlayer::setOnPreparedListener(jmethodID listener, jobject obj) {
    LOGD("jni set on prepared listener.\n");
    this->jmidOnparpared = listener;
    this->objOnPrepared = obj;
}

void AudioPlayer::setMetaDataListener(jmethodID listener, jobject obj) {
    LOGD("jni set metaData listener.\n");
    this->jmidMetadata = listener;
    this->objMetaData = obj;
}

void AudioPlayer::onBaseInfo(const char* key, const char* value){
    if(this->jmidBaseInfo){
        JNIEnv *env;
        this->g_javaVM->AttachCurrentThread(&env, NULL);
        jstring k = env->NewStringUTF(key);
        jstring v = env->NewStringUTF(value);
        env->CallObjectMethod(this->objBaseInfo, this->jmidBaseInfo, k, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
        this->g_javaVM->DetachCurrentThread();
    }
}

void AudioPlayer::onPrepared(const char *s) {
    if(this->jmidOnparpared){
        JNIEnv *env;
        this->g_javaVM->AttachCurrentThread(&env, NULL);
        jstring str_arg = env->NewStringUTF(s);
        env->CallObjectMethod(this->objOnPrepared, this->jmidOnparpared, str_arg);
        env->DeleteLocalRef(str_arg);
        this->g_javaVM->DetachCurrentThread();
    }
}


void AudioPlayer::onError(const char* s, int errorCode) {
    if(this->jmidOnError){
        JNIEnv *env;
        this->g_javaVM->AttachCurrentThread(&env, NULL);
        jstring str_arg = env->NewStringUTF(s);
        env->CallObjectMethod(this->objOnError, this->jmidOnError, str_arg, errorCode);
        env->DeleteLocalRef(str_arg);
        this->g_javaVM->DetachCurrentThread();
    }
}

void AudioPlayer::onGetMetaData(const char *key, const char *value) {
    if(this->jmidMetadata){
        JNIEnv *env;
        this->g_javaVM->AttachCurrentThread(&env, NULL);
        jstring k = env->NewStringUTF(key);
        jstring v = env->NewStringUTF(value);
        env->CallObjectMethod(this->objMetaData, this->jmidMetadata, k, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
        this->g_javaVM->DetachCurrentThread();
    }
}

void AudioPlayer::prepared_fun() {
    LOGD("thread prepared function");

    av_register_all();
    avformat_network_init();
    this->pFormatCtx = avformat_alloc_context();

    AVDictionary* format_opts = NULL;
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    }

    if(avformat_open_input(&this->pFormatCtx, this->path, NULL, &format_opts))
    {
        LOGE("can not open url :%s", this->path);
        this->onError("avformat can not open url.", -1);
        return;
    }

    if(avformat_find_stream_info(this->pFormatCtx, NULL) < 0)
    {
        LOGE("can not find streams from %s", this->path);
        this->onError("can not find streams from url.\n", -1);
        return;
    }

    /*get meta data sent java*/
    AVDictionaryEntry *m = NULL;
    while(m = av_dict_get(pFormatCtx->metadata,"",m,AV_DICT_IGNORE_SUFFIX)) {
        LOGD("key : %s, value : %s\n", m->key, m->value);
        this->onGetMetaData(m->key, m->value);
    }

    for(int i = 0; i < this->pFormatCtx->nb_streams; i++)
    {
        if(this->pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)//得到音频流
        {
            LOGI("AVFormatContext nb streams : %d code_type \n", i);
            this->avCodecParameters = pFormatCtx->streams[i]->codecpar;
            this->avCodec = avcodec_find_decoder(this->avCodecParameters->codec_id);
            if(!this->avCodec){
                LOGE("can not find decoder");
                this->onError("can not find decoder", -1);
                return;
            } else{
                LOGI("find audio codec\n");
                break;
            }
        }
    }

    /*get audio base info sent java*/
    char buf[512];
    int duration  = ( this->pFormatCtx->duration)/1000000;
    sprintf(buf, "%d:%02d:%02d",duration);
    this->onBaseInfo("duration", buf);
    this->onBaseInfo("format", this->pFormatCtx->iformat->name);

    sprintf(buf, "%lld kb/s", (int64_t)this->pFormatCtx->bit_rate / 1000);
    this->onBaseInfo("bit_rate", buf);

    sprintf(buf, "%d Hz", this->avCodecParameters->sample_rate);
    this->onBaseInfo("sample_rate", buf);

    this->avCodecContext = avcodec_alloc_context3(this->avCodec);
    if(!avCodecContext){
        LOGE("can not alloc new decodecctx");
        this->onError("can not alloc new decodecctx\n", -1);
        return;
    }

    if(avcodec_parameters_to_context(this->avCodecContext, this->avCodecParameters) < 0){
        LOGE("can not fill decodecctx");
        this->onError("avcodec_parameters_to_context", -1);
        return;
    }

    if(avcodec_open2(this->avCodecContext, this->avCodec, 0) != 0){
        LOGE("avcodec_open2");
        this->onError("avcodec_open2", -1);
        return;
    }

    LOGD("avcodec open success !\n");

    return;
}
