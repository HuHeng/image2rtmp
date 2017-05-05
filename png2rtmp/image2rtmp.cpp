extern "C"{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavutil/avutil.h"

#include <libavformat/avio.h>
#include <libavutil/file.h>
#include "libavutil/time.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
}

#include "image2rtmp.h"
#include "simplefilter.h"

#include <string>

//png is std::string
//convert png to AVFrame yuv420p
//encode AVFrame
//write packet to rtmp

static const AVRational STREAM_TIME_BASE = {1,1000};
static const enum AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_YUV420P;

struct buffer_data {
    uint8_t *ptr;
    size_t pos; // cur pos
    size_t size; ///< size of the buffer
};

static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    buf_size = FFMIN(buf_size, bd->size - bd->pos);
    if(buf_size < 0)
        return 0;
    printf("ptr:%p size:%zu\n", bd->ptr + bd->pos, bd->size - bd->pos);

    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr + bd->pos, buf_size);
    bd->pos  += buf_size;

    return buf_size;
}

static int64_t buffer_seek(void *opaque, int64_t offset, int whence)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    switch (whence){
        case SEEK_SET:
            bd->pos = 0;
            break;
        case SEEK_END:
            bd->pos = bd->size - 1;
            break;
        default:
            break;
    }
    bd->pos += offset;
    if(bd->pos > bd->size - 1)
        bd->pos = bd->size -1;
    else if(bd->pos < 0)
        bd->pos = 0;
    return bd->pos;
}



int Image2rtmpContext::FFmpegRegist = 0;

Image2rtmpContext::Image2rtmpContext(std::string streamUrl):
    url(streamUrl),
    image_frame(NULL),
    fmt_ctx(NULL),
    enc_ctx(NULL)
{
    if(!FFmpegRegist){
        av_register_all();
        avfilter_register_all();
        avformat_network_init();
        FFmpegRegist = 1;
    }
    image_frame = av_frame_alloc();
    filter_frame = av_frame_alloc();
}

Image2rtmpContext::~Image2rtmpContext()
{
    if(image_frame){
        av_frame_unref(image_frame);
        av_frame_free(&image_frame);
    }
    if(enc_ctx){
        avcodec_free_context(&enc_ctx);
    }
    if(fmt_ctx){
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
    }
}

void Image2rtmpContext::closeOutput()
{
    if(enc_ctx){
        avcodec_free_context(&enc_ctx);
        enc_ctx = NULL;
    }
    if(fmt_ctx){
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
        fmt_ctx = NULL;
    }
}

static const std::string filterDesc = "scale=100:100";

int Image2rtmpContext::ensuringFilter(FrameArgs frameargs)
{
    return videoFilter.Config(frameargs, filterDesc);
}

int Image2rtmpContext::ensuringOutput(AVFrame* image_frame)
{
    if(fmt_ctx != NULL)
        return -1;
    //open output format
    int ret = 0;
    //alloc fmt ctx
    ret = avformat_alloc_output_context2(&fmt_ctx, NULL, "flv", url.c_str());
    if(ret < 0){
        printf("alloc output format failed!\n");
        return -1;
    }
    AVOutputFormat* fmt = fmt_ctx->oformat;
    //find codec
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!codec){
        printf("find encoder failed\n");
        return -1;
    }
    //add stream
    AVStream* st = avformat_new_stream(fmt_ctx, NULL);
    st->time_base = STREAM_TIME_BASE;
    if(!st){
        printf("could not alloc stream\n");
        return -1;
    }
    st->id = fmt_ctx->nb_streams - 1;
    //alloc enc ctx
    enc_ctx = avcodec_alloc_context3(codec);
    if(enc_ctx){
        printf("could not alloc avcodec context\n");
        return -1;
    }
    //config encoder ctx
    enc_ctx->codec_id = codec->id;
    enc_ctx->bit_rate = 800000;
    enc_ctx->width    = image_frame->width;
    enc_ctx->height   = image_frame->height;
    enc_ctx->time_base    = st->time_base;

    enc_ctx->gop_size      = 12; /* emit one intra frame every twelve frames at most */
    enc_ctx->pix_fmt       = STREAM_PIX_FMT;

    //open enc_ctx
    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER){
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    ret = avcodec_open2(enc_ctx, codec, NULL);
    if(ret < 0){
        printf("open codec failed\n");
        return ret;
    }
    ret = avcodec_parameters_from_context(st->codecpar, enc_ctx);
    if (ret < 0) {
        printf("copy parameters failed\n");
        return ret;
    }
    /* open the output file, if needed */
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx->pb, url.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", url, av_err2str(ret));
            return ret;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return ret;
    }
    return 0;
}

int Image2rtmpContext::decodeImage(std::string image, AVFrame* frame)
{
    AVFormatContext *fmt_ctx = NULL;
    AVInputFormat* inputformat = NULL;
    AVIOContext *avio_ctx = NULL;
    uint8_t *buffer = NULL, *avio_ctx_buffer = NULL;
    size_t buffer_size, avio_ctx_buffer_size = 4096;
    int ret = 0;
    struct buffer_data bd = { 0 };
    //decode
    AVPacket pkt;
    AVCodecContext* dec_ctx;
    AVCodec* dec;
    AVStream* st;
    int stream_index;

    /* fill opaque structure used by the AVIOContext read callback */
    bd.ptr  = (uint8_t*)image.data();
    bd.size = image.size();
    bd.pos = 0;
    printf("buffer_size: %zu\n", bd.size);
    if (!(fmt_ctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    printf("avio_ctx_buffer: %p\n", avio_ctx_buffer);
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, &bd, &read_packet, NULL, &buffer_seek);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    fmt_ctx->pb = avio_ctx;
    fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
    //
    inputformat = av_find_input_format("png_pipe");
    ret = avformat_open_input(&fmt_ctx, NULL, inputformat, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
        goto end;
    }
    stream_index = ret;
    dec = avcodec_find_decoder(fmt_ctx->streams[stream_index]->codecpar->codec_id);
    if(!dec){
        printf("find codec error\n");
        goto end;
    }

    dec_ctx = avcodec_alloc_context3(dec);
   /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_index]->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy codec parameters to decoder context\n");
        goto end;
    }

    av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open decoder\n");
        goto end;
    }

    av_init_packet(&pkt);
    ret = av_read_frame(fmt_ctx, &pkt);
    if(ret <0 ){
        printf("read packet error, %s\n", av_err2str(ret));
        goto end;
    }

    ret = avcodec_send_packet(dec_ctx, &pkt);
    if(ret < 0){
        printf("send packet error!\n");
        goto end;
    }
    while(1){
        ret = avcodec_receive_frame(dec_ctx, frame);
        if(ret == 0){
           //got frame and send to frame queue
           printf("got frame, height: %d\n", frame->height);
           break;
        }
        else{
            break;
        }
    }
end:
    avformat_close_input(&fmt_ctx);
    /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        av_freep(&avio_ctx);
    }

    if (ret < 0) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}

int Image2rtmpContext::ConsumeImage(std::string image, long pts)
{
    int ret = decodeImage(image, image_frame);
    if(ret < 0){
        printf("decode image failed! count: %d\n", frameCount);
    }
    //trans to yuv420p, config filter box
    FrameArgs frameargs = {image_frame->height,image_frame->width,
                           image_frame->format,STREAM_TIME_BASE};
    ret = ensuringFilter(frameargs);
    if(ret < 0){
        printf("config filter failed\n");
        return ret;
    }

    //send frame to filter
    ret = videoFilter.PushFrame(image_frame);
    av_frame_unref(image_frame);
    if(ret < 0){
        printf("push frame failed\n");
        return ret;
    }
    ret = videoFilter.PopFrame(filter_frame);
    if(ret < 0){
        return ret;
    }
    //ensure output file
    int ret = ensuringOutput(filter_frame);
    if(ret < 0){
        return ret;
    }
    write_video_frame(filter_frame);
    //write frame
    return 1;

}
