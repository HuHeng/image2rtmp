#include "simplefilter.h"
#include <string.h>

extern "C"{
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}


static const enum AVPixelFormat OUTPUT_PIX_FMT = AV_PIX_FMT_YUV420P;

SimpleFilter::SimpleFilter():
    filter_graph(NULL),
    buffersrc_ctx(NULL),
    buffersink_ctx(NULL),
    fa{0}
{

}

SimpleFilter::~SimpleFilter()
{
    avfilter_graph_free(&filter_graph);
}

int SimpleFilter::Config(FrameArgs frameargs, std::string filterDesc)
{
    if(buffersink_ctx != NULL &&
            buffersrc_ctx != NULL &&
            filter_graph != NULL &&
            frameargs == fa){
        //no need config
        return true;
    }
    int ret = 0;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    avfilter_graph_free(&filter_graph);

    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        av_log(NULL, AV_LOG_ERROR, "AVERROR(ENOMEM)=%d\n", ret);
        goto end;
    }

    char videoargs[128];
    snprintf(videoargs, sizeof(videoargs),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frameargs.width, frameargs.height,
             frameargs.fmt,
             frameargs.time_base.num, frameargs.time_base.den,
             frameargs.width, frameargs.height);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       videoargs, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */
    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    //
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filterDesc.c_str(),
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if(ret >= 0){
        fa = frameargs;
    }
    return ret;
}

int SimpleFilter::PushFrame(AVFrame* frame)
{
    int ret;
    ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if(ret < 0){
        printf("push frame failed\n");
    }
    return ret;
}

int SimpleFilter::PopFrame(AVFrame* frame)
{
    int ret;
    ret = av_buffersink_get_frame(buffersink_ctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
        return ret;
    }
    if (ret < 0){
        printf("pop frame failed! %d\n",ret);
        return ret;
    }
    //success
    return 1;
}
