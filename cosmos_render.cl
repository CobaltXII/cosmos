__stringify
(
	__kernel void n_body_render(float inv_scale, int n, int res, __global float4* state, __global unsigned int* output, __global unsigned int* thermal_colormap)
	{
		// Get the index of the pixel to be processed.

		int global_id = get_global_id(0);

		// Get the position of the pixel to be processed.

		int x = global_id % res;
		int y = global_id / res;

		// Get the half dimensions.

		int h_res = res / 2;

		// Store the additive sums.

		float w = 0.0f;

		// Calculate the inverse distance squared from the pixel to each
		// particle in the n-body simulation. Add this distance to the
		// accumulator.

		for (int i = 0; i < n; i++)
		{
			float dx = x - (state[i].x / inv_scale + h_res);
			float dy = y - (state[i].y / inv_scale + h_res);

			float d = dx * dx + dy * dy;

			// PARAM: The 1024.0f can be reduced to make blobs smaller. It can
			// be increased to make blobs larger. The 0.0256f can be increased
			// to make blobs less prominent. Changing it to 0.0f is a bad
			// idea.

			float iw = 1.0f / (d * (inv_scale / 1024.0f) + 0.0256f);

			w += iw;
		}

		// Clamp the accumulator.

		if (w > 255.0f)
		{
			w = 255.0f;
		}

		output[y * res + x] = 255 << 24 | thermal_colormap[int(w)];
	}
);