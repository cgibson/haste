#include "host.h"

// initialize variables
Render host::render = DEFAULT_RENDER;
Camera host::camera = DEFAULT_CAMERA;
lua_State *host::lstate = NULL;
MetaObject *host::meta_chunk = NULL;
uint64_t host::num_objs = 0;
LightObject *host::light_list = NULL;
uint64_t host::num_lights = 0;
Material *host::mat_list = NULL;
uint64_t host::num_mats = 0;
void *host::obj_chunk = NULL;
uint64_t host::obj_chunk_size = 0;

inline static int ConvertSMVer2Cores(int major, int minor)
{
    // Defines for GPU Architecture types (using the SM version to determine the # of cores per SM
    typedef struct {
        int SM; // 0xMm (hexidecimal notation), M = SM Major version, and m = SM minor version
        int Cores;
    } sSMtoCores;

    sSMtoCores nGpuArchCoresPerSM[] =
    { { 0x10,  8 },
    { 0x11,  8 },
    { 0x12,  8 },
    { 0x13,  8 },
    { 0x20, 32 },
    { 0x21, 48 },
    {   -1, -1 }
    };

    int index = 0;
    while (nGpuArchCoresPerSM[index].SM != -1) {
        if (nGpuArchCoresPerSM[index].SM == ((major << 4) + minor) ) {
            return nGpuArchCoresPerSM[index].Cores;
        }
        index++;
    }
    fprintf(stderr, "MapSMtoCores undefined SMversion %d.%d!\n", major, minor);
    exit(EXIT_FAILURE);
    return -1;
}

void host::InsertIntoLightList(ObjType type, uint64_t offset) {
	// expand the light list
	light_list = (LightObject *)realloc(light_list, (num_lights + 1) * sizeof(LightObject));
	
	// save the details
	light_list[num_lights].type = type;
	light_list[num_lights].offset = offset;
	num_lights++;
}

uint64_t host::InsertIntoMaterialList(Material *mat) {
    // linear search to see if the material is already in the list
    for (uint64_t i = 0; i < num_mats; i++) {
        if (MaterialEqual(&(mat_list[i]), mat)) {
            // return the reference to the existing material
            return i;
        }
    }

    // did not exist, so insert it into the list
    uint64_t id = num_mats++;
    mat_list = (Material *)realloc(mat_list, (num_mats + 1) * sizeof(Material));
    mat_list[id] = *mat;

    return id;
}

void host::DestroyScene() {
    // release host memory if it was allocated
    if (meta_chunk != NULL) {
        free(meta_chunk);
        meta_chunk = NULL;
    }
    if (light_list != NULL) {
        free(light_list);
        light_list = NULL;
    }
    if (mat_list != NULL) {
        free(mat_list);
        mat_list = NULL;
    }
    if (obj_chunk != NULL) {
        free(obj_chunk);
        obj_chunk = NULL;
    }

    // reset counters
    num_objs = 0;
    num_lights = 0;
    num_mats = 0;
    obj_chunk_size = 0;
}

std::vector<int> host::QueryDevices() {
    int device_count = 0;
    CUDA_SAFE_CALL(cudaGetDeviceCount(&device_count));
    if (device_count < 1) {
        fprintf(stderr, "No suitable CUDA devices found!\n");
        exit(EXIT_FAILURE);
    }

    std::vector<int> device_ids;

    for (int i = 0; i < device_count; i++) {
        cudaDeviceProp device_prop;
        CUDA_SAFE_CALL(cudaGetDeviceProperties(&device_prop, i));

        int compute_cap_major = device_prop.major;
        int compute_cap_minor = device_prop.minor;
        int core_count = ConvertSMVer2Cores(compute_cap_major, compute_cap_minor) * device_prop.multiProcessorCount;
        float clock_speed = device_prop.clockRate * 1e-6f;

        float mem_size = device_prop.totalGlobalMem / 1024.0f / 1024.0f;

        if (compute_cap_major >= 2) {
            device_ids.push_back(i);
            printf("\t[%d] %s (%d.%d, %d cores, %.2f GHz, %.2f MB)\n",
                i,
                device_prop.name,
                compute_cap_major,
                compute_cap_minor,
                core_count,
                clock_speed,
                mem_size);
        } else {
            printf("\t[%d] %s (%d.%d not usable)\n",
                i,
                device_prop.name,
                compute_cap_major,
                compute_cap_minor);
        }
    }

    if (device_ids.size() == 0) {
        fprintf(stderr, "No suitable CUDA devices found!\n");
        exit(EXIT_FAILURE);
    }

    return device_ids;
}

char *host::GetOutputBaseName(const char *input_filename) {
    const char *temp = basename(input_filename);
    int name_len = strlen(temp);

    // find the last '.' in the file name
    int ext_loc = -1;
    for (int i = name_len - 1; i > 0; i--) {
        if (temp[i] == '.') {
            ext_loc = i;
            break;
        }
    }
    
    char *temp2 = NULL;
    if (ext_loc > 0) {
        // copy everything up until the extension
        temp2 = (char *)malloc(sizeof(char) * (ext_loc + 1));
        strncpy(temp2, temp, ext_loc);
        temp2[ext_loc] = '\0';     
    } else {
        // just copy the entire thing
        temp2 = (char *)malloc(sizeof(char) * (name_len + 1));
        strncpy(temp2, temp, name_len);
        temp2[name_len] = '\0';
    }
    
    return temp2;
}
