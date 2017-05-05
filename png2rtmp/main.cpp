extern "C"
{
#include "libavutil/file.h"
}

#include "image2rtmp.h"

#include <time.h>
#include <string>


int main(int argc, char *argv[])
{
    Image2rtmpContext* i2r = new Image2rtmpContext("a.mp4");

    double start = clock();
    if(argc < 2){
        printf("input paramters\n");
        return 0;
    }
    uint8_t* buffer = NULL;
    size_t buffer_size;
    int ret = av_file_map(argv[1], &buffer, &buffer_size, 0, NULL);
    std::string png((char*)buffer, buffer_size);

    i2r->ConsumeImage(png, 0);


    //printf("frame height: %d\n", frame->height);
    double finish = clock();
    printf("decode time: %f\n", (finish-start)/CLOCKS_PER_SEC);
    return 0;
}
