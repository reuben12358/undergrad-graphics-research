#include "vec.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <climits>
#include <fstream>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <png.h>

// road map
// - create basic mesh (squares whose centers are squares)
// - be able to read files
// - create 1 object rendered into mesh

struct data_vertex {
    float * data;
};

struct data_geometry {
    vec4 gl_position;
    float * data;
};

struct data_fragment {
    float * data;
};

struct data_output {
    vec4 output_color;
};

typedef unsigned int Pixel;

inline Pixel make_pixel(int r, int g, int b)
{
    return (r<<24)|(g<<16)|(b<<8)|0xff;
}

inline void from_pixel(Pixel p, int& r, int& g, int& b)
{
    r = (p>>24)&0xff;
    g = (p>>16)&0xff;
    b = (p>>8)&0xff;
}

typedef void (*shader_v)(const data_vertex&, data_geometry&,const float *);
typedef void (*shader_f)(const data_fragment&, data_output&,const float *);

struct driver_state {
    float * vertex_data = 0;
    int num_vertices = 0;
    int floats_per_vertex = 0;
    int * index_data = 0;
    int num_triangles = 0;
    float * uniform_data = 0;

    int image_width = 0;
    int image_height = 0;
    Pixel * image_color = 0;
    float * image_depth = 0;

    void (*vertex_shader)(const data_vertex& in, data_geometry& out, const float * uniform_data);
    void (*fragment_shader)(const data_fragment& in, data_output& out, const float * uniform_data);

    driver_state();
    ~driver_state();
};

driver_state::driver_state()
{
}

driver_state::~driver_state()
{
    delete [] image_color;
    delete [] image_depth;
}

// dump_png.cpp

// Dump an image to file.
void dump_png(Pixel* data,int width,int height,const char* filename)
{
    FILE* file=fopen(filename,"wb");
    assert(file);

    png_structp png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING,0,0,0);
    assert(png_ptr);
    png_infop info_ptr=png_create_info_struct(png_ptr);
    assert(info_ptr);
    bool result=setjmp(png_jmpbuf(png_ptr));
    assert(!result);
    png_init_io(png_ptr,file);
    int color_type=PNG_COLOR_TYPE_RGBA;
    png_set_IHDR(png_ptr,info_ptr,width,height,8,color_type,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);

    Pixel** row_pointers=new Pixel*[height];
    for(int j=0;j<height;j++) row_pointers[j]=data+width*(height-j-1);
    png_set_rows(png_ptr,info_ptr,(png_byte**)row_pointers);
    png_write_png(png_ptr,info_ptr,PNG_TRANSFORM_BGR|PNG_TRANSFORM_SWAP_ALPHA,0);
    delete[] row_pointers;
    png_destroy_write_struct(&png_ptr,&info_ptr);
    fclose(file);
}

// Read an image from file.
void read_png(Pixel*& data,int& width,int& height,const char* filename)
{
    FILE *file = fopen(filename, "rb");
    assert(file);

    unsigned char header[8];
    int num_read=fread(&header, 1, sizeof header, file);
    assert(num_read==sizeof header);
    int ret_sig=png_sig_cmp((png_bytep)header, 0, sizeof header);
    assert(!ret_sig);
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    assert(png_ptr);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    assert(info_ptr);
    png_infop end_info = png_create_info_struct(png_ptr);
    assert(end_info);
    png_init_io(png_ptr, file);
    png_set_sig_bytes(png_ptr, sizeof header);
    png_read_info(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if(color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    if(bit_depth == 16)
        png_set_strip_16(png_ptr);

    if(bit_depth < 8)
        png_set_packing(png_ptr);
    
    if(color_type == PNG_COLOR_TYPE_GRAY_ALPHA || color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_swap_alpha(png_ptr);

    if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    if(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_bgr(png_ptr);

    if(color_type == PNG_COLOR_TYPE_RGB)
        png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

    height = png_get_image_height(png_ptr, info_ptr);

    width = png_get_image_width(png_ptr, info_ptr);

    data = new Pixel[width * height];

    png_read_update_info(png_ptr, info_ptr);

    for(int i = 0; i < height; i++)
        png_read_row(png_ptr, (png_bytep)(data + (height-i-1) * width), 0);

    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
    fclose(file);
}

void Usage(const char* prog_name)
{
    std::cerr<<"Usage: "<<prog_name<<" -i <input-file> [ -s <solution-file> ] [ -o <stats-file> ]"<<std::endl;
    std::cerr<<"    <input-file>      File with commands to run"<<std::endl;
    std::cerr<<"    <solution-file>   File with solution to compare with"<<std::endl;
    std::cerr<<"    <stats-file>      Dump statistics to this file rather than stdout"<<std::endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    
    driver_state state;

    const char* solution_file = 0;
    const char* input_file = 0;
    const char* statistics_file = 0;

    // Parse commandline options
    while(1)
    {
        int opt = getopt(argc, argv, "s:i:o:");
        if(opt==-1) break;
        switch(opt)
        {
            case 's': solution_file = optarg; break;
            case 'i': input_file = optarg; break;
            case 'o': statistics_file = optarg; break;
        }
    }

    // Sanity checks
    if(!input_file)
    {
        std::cerr<<"Test file required.  Use -i."<<std::endl;
        Usage(argv[0]);
    }

    // change to be able to read file here
    FILE* F = fopen(input_file,"r");
    if(!F)
    {
        printf("Failed to open file '%s'\n",input_file);
        exit(EXIT_FAILURE);
    }

    char buff[1000];
    ivec3 e;

    while(fgets(buff, sizeof(buff), F))
    {
        std::stringstream ss(buff);
        std::string item,name;

        // If we did not get a line, the line is empty, or the line is a
        // comment, then move on.
        if(!(ss>>item) || !item.size() || item[0]=='#') continue;
        if(item=="size")
        {
            ss>>state.image_width>>state.image_height;
        }
        // insert rest of parsing code here
        else
        {
            // Check for parse errors.
            int len=strlen(buff);
            if(buff[len-1]=='\n') buff[len-1]=0;
            printf("Unrecognized command: '%s'\n",buff);
            exit(EXIT_FAILURE);
        }
    }
    fclose(F);
    // state.image_width = 500;
    // state.image_height = 500;
    state.image_color = new Pixel[state.image_width * state.image_height];
    state.image_depth = new float[state.image_width * state.image_height];
 
    // blank canvas
    std::fill(state.image_color, state.image_color + (state.image_width * state.image_height), make_pixel(50,50,250));
    std::fill(state.image_depth, state.image_depth + (state.image_width * state.image_height), 2);

    // vertical lines p1
    for (int i = 100; i < state.image_width; i = i + 100) {
        for (int j = 0; j < state.image_height; ++j) {
            state.image_color[(j * state.image_width) + i] = 
            make_pixel(255,0,0);
        }
    }
    // horizontal lines p1
    for (int i = 0; i < state.image_width; ++i) {
        for (int j = 100; j < state.image_height; j = j + 100) {
            state.image_color[(j * state.image_width) + i] = 
            make_pixel(255,0,0);
        }
    }
    // vertical lines p2
    for (int i = 50; i < state.image_width; i = i + 100) {
        for (int j = 0; j < state.image_height; ++j) {
            state.image_color[(j * state.image_width) + i] = 
            make_pixel(0,255,0);
        }
    }
    // horizontal lines p2
    for (int i = 0; i < state.image_width; ++i) {
        for (int j = 50; j < state.image_height; j = j + 100) {
            state.image_color[(j * state.image_width) + i] = 
            make_pixel(0,255,0);
        }
    }

    // Save the computed solution to file
    dump_png(state.image_color,state.image_width,state.image_height,"output.png");

    return 0;
}