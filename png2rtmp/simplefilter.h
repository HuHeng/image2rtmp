#ifndef SIMPLEFILTER_H
#define SIMPLEFILTER_H
extern "C"
{
#include "libavutil/rational.h"
}
#include <string>

//a simple filter was used for filtering video frame

typedef struct FrameArgs
{
    int height;
    int width;
    int fmt;
    AVRational time_base;

    bool operator==(FrameArgs &a)
    {
        return height == a.height &&
                width == a.width &&
                fmt == a.fmt &&
                time_base.den == a.time_base.den &&
                time_base.num == a.time_base.num;
    }

}FrameArgs;


struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

class SimpleFilter {
public:
    SimpleFilter();
    ~SimpleFilter();
    int Config(FrameArgs frameargs, std::string filterDesc);
    int PushFrame(AVFrame* frame);
    int PopFrame(AVFrame* frame);

private:
    AVFilterGraph* filter_graph;
    AVFilterContext* buffersrc_ctx;
    AVFilterContext* buffersink_ctx;
    FrameArgs fa;
};

#endif // SIMPLEFILTER_H
