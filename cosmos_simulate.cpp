/*

### cosmos_simulate

To compile, use the command

```bash
clang++ cosmos_simulate.cpp -o cosmos_simulate -std=c++11 -lOpenCL
```

To run, use the command

```bash
./cosmos_simulate <frames> [bodies=24576]
```

This will cause cosmos_simulate to output a bunch of frames to the directory
that it was started in. These frames will be binary files, copied directly
from OpenCL output.

## Even more speed

To speed up simulations and renders even more, compile with

```bash
clang++ cosmos_tool.cpp -o cosmos_tool -std=c++11 -lOpenCL -Ofast -march=native
```

*/

#include <iostream>
#include <sstream>
#include <fstream>

#include <ctime>
#include <cmath>

#include <memory>

// Random value between 0.0f and 1.0f.

inline float rand_01()
{
	return float(rand()) / float(RAND_MAX);
}

// Convert degrees to radians.

inline float degrad(float x)
{
	return 2.0f * M_PI * (x / 360.0f);
}

// Include stb_image_write.

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"

// Include OpenCL.

#ifdef __APPLE__

#define CL_SILENCE_DEPRECATION

#include <OpenCL/OpenCL.h>

#else

#include <CL/cl.h>

#endif

// Include the kernel source.

#define __stringify(source) #source

const char* kernel_source =

#include "cosmos_simulate.cl"

#undef __stringify

size_t kernel_source_size = strlen(kernel_source);

// Include the thermal colormap.

#include "thermal_colormap.h"

// Write a message to std::cout.

void say(std::string message)
{
	std::cout << message << std::endl;
}

// Entry point.

int main(int argc, char** argv)
{
	// Parse command line arguments.

	if (argc != 2 && argc != 3)
	{
		std::cout << "Usage: " << argv[0] << " <frames> [bodies=24576]" << std::endl;

		return EXIT_FAILURE;
	}

	int frames = atoi(argv[1]);

	// Create variables to hold return codes.

	cl_int r_code;

	cl_int r_code1;
	cl_int r_code2;

	// Create identifier objects to hold information about the available
	// platforms and available devices.

	cl_platform_id platform_id = NULL;

	cl_device_id device_id = NULL;

	// Create unsigned integer objects to hold the amount of available
	// platforms and available devices.

	cl_uint num_platforms;

	cl_uint num_devices;

	// Get the first available platform and store the amount of available
	// platforms.

	clGetPlatformIDs(1, &platform_id, &num_platforms);

	// Get the first available device on the first available platform. Store
	// the amount of available devices. This device will be referred to as the
	// 'default device'.

	clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_DEFAULT, 1, &device_id, &num_devices);

	// Create an OpenCL context on the default device.

	cl_context context = clCreateContext(0, 1, &device_id, NULL, NULL, &r_code);

	// Make sure the OpenCL context was created successfully.

	if (r_code != CL_SUCCESS)
	{
		say("Could not create an OpenCL context.");

		return EXIT_FAILURE;
	}

	// Create an OpenCL command queue.

	cl_command_queue command_queue = clCreateCommandQueue(context, device_id, 0, &r_code);

	// Make sure the OpenCL command queue was created successfully.

	if (r_code != CL_SUCCESS)
	{
		say("Could not create an OpenCL command queue.");

		return EXIT_FAILURE;
	}

	// Allocate CPU memory for the n-body simulation.

	// PARAM: The n-body count.

	cl_int n = 1024 * 24;

	if (argc == 3)
	{
		n = atoi(argv[2]);
	}

	cl_float4* state1 = (cl_float4*)malloc(n * sizeof(cl_float4));
	cl_float4* state2 = (cl_float4*)malloc(n * sizeof(cl_float4));

	// Make sure both arrays were allocated successfully.

	if (!state1 || !state2)
	{
		say("Could not allocate local CPU memory.");

		return EXIT_FAILURE;
	}

	// Initilize the n-body simulation.

	// PARAM: The initial seed.

	srand(time(NULL));

	// PARAM: The initial spawn size.

	const float xr = 16000.0f;
	const float yr = 16000.0f;

	for (int i = 0; i < n; i++)
	{
		// Generate a random body.

		// PARAM: The body creation routine.

		cl_float ang = rand_01() * 2.0f * M_PI;

		cl_float rad = rand_01();

		cl_float x = (xr * rad) * cos(ang);
		cl_float y = (yr * rad) * sin(ang);

		cl_float vx = cos(ang + degrad(90.0f)) * rad * 64.0f;
		cl_float vy = sin(ang + degrad(90.0f)) * rad * 64.0f;

		// Write the body to the first state.

		state1[i] = {x, y, vx, vy};
	}

	// Clear the second state.

	memset(state2, 0, n * sizeof(cl_float4));

	// Allocate GPU memory for the n-body simulation.

	cl_mem gpu_state1 = clCreateBuffer(context, CL_MEM_READ_WRITE, n * sizeof(cl_float4), NULL, &r_code1);
	cl_mem gpu_state2 = clCreateBuffer(context, CL_MEM_READ_WRITE, n * sizeof(cl_float4), NULL, &r_code2);

	// Make sure both arrays were allocated successfully.

	if (r_code1 != CL_SUCCESS || r_code2 != CL_SUCCESS)
	{
		say("Could not allocate GPU memory.");

		return EXIT_FAILURE;
	}

	// Copy the contents of the CPU n-body simulation memory to the GPU n-body
	// simulation memory.

	r_code1 = clEnqueueWriteBuffer(command_queue, gpu_state1, CL_TRUE, 0, n * sizeof(cl_float4), state1, 0, NULL, NULL);
	r_code2 = clEnqueueWriteBuffer(command_queue, gpu_state2, CL_TRUE, 0, n * sizeof(cl_float4), state2, 0, NULL, NULL);

	// Make sure both arrays were copied successfully.

	if (r_code1 != CL_SUCCESS || r_code2 != CL_SUCCESS)
	{
		say("Could not copy CPU memory to GPU memory.");

		return EXIT_FAILURE;
	}

	// Create an OpenCL program from the kernel source.

	cl_program program = clCreateProgramWithSource(context, 1, (const char**)&kernel_source, (const size_t*)&kernel_source_size, &r_code);

	// Make sure the OpenCL program was created successfully.

	if (r_code != CL_SUCCESS)
	{
		say("Could not create an OpenCL program.");

		return EXIT_FAILURE;
	}

	// Build the OpenCL program.

	r_code = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

	// Make sure the OpenCL program was built successfully.

	if (r_code != CL_SUCCESS)
	{
		say("Could not build an OpenCL program.");

		return EXIT_FAILURE;
	}

	// Create the OpenCL kernel from the function "n_body_cl" within the
	// OpenCL program.

	cl_kernel kernel = clCreateKernel(program, "n_body_cl", &r_code);

	// Make sure the OpenCL kernel was created successfully.

	if (r_code != CL_SUCCESS)
	{
		say("Could not create an OpenCL kernel.");

		return EXIT_FAILURE;
	}

	// Set the n-body count parameter of the kernel.

	clSetKernelArg(kernel, 2, sizeof(cl_int), &n);

	// Set the state parameters of the kernel.

	clSetKernelArg(kernel, 3, sizeof(cl_mem), (void*)&gpu_state1);
	clSetKernelArg(kernel, 4, sizeof(cl_mem), (void*)&gpu_state2);

	// Get the simulation starting time.

	time_t sim_start = time(NULL);

	// Start the simulation!

	for (int i = 0; i < frames; i++)
	{
		// Get the starting time.

		clock_t begin = clock();

		// Set the timestep and softening parameters.

		cl_float gpu_float_args[2] = {float(i + 1), float(i + 1)};

		// PARAM: gpu_float_args[0] is the timestep.
		// PARAM: gpu_float_args[1] is the softening.

		clSetKernelArg(kernel, 0, sizeof(cl_float), &(gpu_float_args[0]));
		clSetKernelArg(kernel, 1, sizeof(cl_float), &(gpu_float_args[1]));

		// Do one iteration of the n-body simulation.

		size_t global_work_size = n;

		// PARAM: local_work_size should be modified depending on your GPU.
		// This should be tested on a realtime renderer first, such as
		// CobaltXII/boiler/experimental/n_body_cl/.

		size_t local_work_size = 256;

		clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &global_work_size, &local_work_size, 0, NULL, NULL);

		// Read state2 back into local CPU memory.

		clEnqueueReadBuffer(command_queue, gpu_state2, CL_TRUE, 0, n * sizeof(cl_float4), state2, 0, NULL, NULL);

		// Get the iteration end time.

		clock_t end_iteration = clock();

		// Export state2.

		std::stringstream name_builder;

		name_builder << "frame_" << i << ".dat";

		std::ofstream frame(name_builder.str());

		frame.write((const char*)state2, n * sizeof(cl_float4));

		frame.close();

		// Get the export end time.

		clock_t end_export = clock();

		// Print frame data.

		std::cout << "Frame " << i << " done in " << float(end_export - begin) / float(CLOCKS_PER_SEC) << " s (" << float(end_iteration - begin) / float(CLOCKS_PER_SEC) << " s on calculations, " << float(end_export - end_iteration) / float(CLOCKS_PER_SEC) << " s on export)" << std::endl;

		// Swap buffers.

		std::swap(gpu_state1, gpu_state2);

		clSetKernelArg(kernel, 3, sizeof(cl_mem), (void*)&gpu_state1);
		clSetKernelArg(kernel, 4, sizeof(cl_mem), (void*)&gpu_state2);
	}

	// Get the simulation ending time.

	time_t sim_end = time(NULL);

	// Print the overall runtime details.

	std::cout << "Simulated and output " << frames << " frames in " << sim_end - sim_start << " s (" << float(sim_end - sim_start) / float(frames) << " s/frame)" << std::endl;

	// Exit successfully.

	return EXIT_SUCCESS;
}