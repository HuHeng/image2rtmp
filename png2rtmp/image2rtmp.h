#ifndef IMAGE2RTMP_H
#define IMAGE2RTMP_H

#include "simplefilter.h"
#include <string>


/* avoid a temporary address return build error in c++ */
#undef av_err2str
#define av_err2str(errnum) \
    av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), AV_ERROR_MAX_STRING_SIZE, errnum)

#undef av_ts2str
#define av_ts2str(ts) \
    av_ts_make_string((char*)__builtin_alloca(AV_TS_MAX_STRING_SIZE), ts)

#undef av_ts2timestr
#define av_ts2timestr(ts, tb) \
    av_ts_make_time_string((char*)__builtin_alloca(AV_TS_MAX_STRING_SIZE), ts, tb)

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;

class Image2rtmpContext
{
public:
    static int FFmpegRegist;
    Image2rtmpContext(std::string url);
    ~Image2rtmpContext();

    int ConsumeImage(std::string image, long pts);


private:
    int decodeImage(std::string image, AVFrame* frame);
    int ensuringOutput(AVFrame* frame);
    void closeOutput();

    //ensured filter was active, otherwise config the filter
    int ensuringFilter(FrameArgs frameargs);

    std::string url;            //output stream url
    int frameCount;
    int height;
    int width;
    int64_t startPts;
    AVFrame* image_frame;
    AVFrame* filter_frame;
    AVFormatContext* fmt_ctx;
    AVCodecContext* enc_ctx;    //h264 encode

    int stream_index;

    SimpleFilter videoFilter;
};




#endif // IMAGE2RTMP_H
