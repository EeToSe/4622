/*****************************************************************************/
// File: motion_main.cpp
// Author: David Taubman
// Last Revised: 30 September, 2007
/*****************************************************************************/

#include "../io_bmp/io_bmp.h"
#include "image_comps.h"
#include "motion.h"
#include <iostream>
#include <math.h>
#include <cmath>
#include <iomanip>
#include <algorithm>

#define vertical 1   
#define horizontal 2
using namespace std;
bool cmp(float x, float y) {
	return x > y;
}

/* ========================================================================= */
/*                 Implementation of `my_image_comp' functions               */
/* ========================================================================= */

/*****************************************************************************/
/*                  my_image_comp::perform_boundary_extension                */
/*                          Point-symmetry extension			             */
void my_image_comp::perform_boundary_extension()
{
	int r, c;

	// First extend upwards
	float *first_line = buf;
	for (r = 1; r <= border; r++)
		for (c = 0; c < width; c++)
			first_line[-r * stride + c] = 2 * first_line[c] - first_line[r*stride + c];

	// Now extend downwards
	float *last_line = buf + (height - 1)*stride;
	for (r = 1; r <= border; r++)
		for (c = 0; c < width; c++)
			last_line[r*stride + c] = 2 * last_line[c] - last_line[-r * stride + c];

	// Now extend all rows to the left and to the right
	float *left_edge = buf - border * stride;
	float *right_edge = left_edge + width - 1;
	for (r = height + 2 * border; r > 0; r--, left_edge += stride, right_edge += stride)
		for (c = 1; c <= border; c++)
		{
			left_edge[-c] = 2 * left_edge[0] - left_edge[c];
			right_edge[c] = 2 * right_edge[0] - right_edge[-c];
		}
}

/*****************************************************************************/
/*           my_image_comp::gaussian_filter   separable convolution         */
/*****************************************************************************/
void gaussian_filter(my_image_comp *in, my_image_comp *out, int sigma, int pattern) {
	constexpr auto PI = 3.141592653589793F;
	int FILTER_EXTENT = 3 * sigma;
	int FILTER_DIM = (2 * FILTER_EXTENT + 1);

	float *filter_buf = new float[FILTER_DIM];
	float *mirror_psf = filter_buf + FILTER_EXTENT;

	for (int t = -FILTER_EXTENT; t <= FILTER_EXTENT; t++)
		mirror_psf[t] = exp(-float(t*t) / (2 * sigma*sigma)) / (sigma*sqrt(2 * PI));

	// Check for consistent dimensions
	assert(in->border >= FILTER_EXTENT);
	assert((out->height <= in->height) && (out->width <= in->width));

	// Renormalize to keep the DC gain is 1
	float gain = 0;
	for (int t = -FILTER_EXTENT; t <= FILTER_EXTENT; t++)
		gain += mirror_psf[t];
	for (int t = -FILTER_EXTENT; t <= FILTER_EXTENT; t++)
		mirror_psf[t] = mirror_psf[t] / gain;

	//cout << "***** Renormalized Gaussain Filter 1D Kernel ****";
	//for (int t = -FILTER_EXTENT; t <= FILTER_EXTENT; t++)
	//	cout << mirror_psf[t] << endl;


	if (pattern = vertical) {
		for (int r = 0; r < out->height; r++)
			for (int c = 0; c < out->width; c++) {
				float *ip = in->buf + r * in->stride + c;
				float *op = out->buf + r * out->stride + c;
				float sum = 0.0F;
				for (int t = -FILTER_EXTENT; t <= FILTER_EXTENT; t++)
					sum += ip[t*in->stride] * mirror_psf[t];
				*op = sum;
			}
	}
	if (pattern = horizontal) {
		for (int r = 0; r < out->height; r++)
			for (int c = 0; c < out->width; c++) {
				float *ip = in->buf + r * in->stride + c;
				float *op = out->buf + r * out->stride + c;
				float sum = 0.0F;
				for (int t = -FILTER_EXTENT; t <= FILTER_EXTENT; t++)
					sum += ip[t] * mirror_psf[t];
				*op = sum;
			}
	}
}

/*****************************************************************************/
/*                                    main                                   */
/*****************************************************************************/

int
main(int argc, char *argv[])
{
	if (argc != 6)
	{
		fprintf(stderr, "Usage: %s <in bmp file> <out bmp file> <sigma> <block half size: H> <search range: S>\n", argv[0]);
		return -1;
	}
	int sigma = atoi(argv[3]);
	int EXTENT = 3 * sigma;
	int H = atoi(argv[4]);
	int S = atoi(argv[5]);
	cout << "***** Command Line Arguments *****\n";
	cout << "Sigma: " << sigma << endl;
	cout << "Block half size: " << H << endl;
	cout << "Search range: " << S << endl;

	int err_code = 0;
	try {
		// Read the input image
		bmp_in in;
		if ((err_code = bmp_in__open(&in, argv[1])) != 0)
			throw err_code;

		int width = in.cols, height = in.rows;
		int n, num_comps = in.num_components;
		my_image_comp *input_comps = new my_image_comp[num_comps];
		for (n = 0; n < num_comps; n++)
			input_comps[n].init(height, width, 3 * sigma); // Leave a border of 3 standard deviation

		int r, c; // Declare row and column index
		io_byte *line = new io_byte[width*num_comps];
		for (r = height - 1; r >= 0; r--)
		{ // "r" holds the true row index we are reading, since the image is
		  // stored upside down in the BMP file.
			if ((err_code = bmp_in__get_line(&in, line)) != 0)
				throw err_code;
			for (n = 0; n < num_comps; n++)
			{
				io_byte *src = line + n; // Points to first sample of component n
				float *dst = input_comps[n].buf + r * input_comps[n].stride;
				for (int c = 0; c < width; c++, src += num_comps)
					dst[c] = (float)*src; // The cast to type "float" is not
						  // strictly required here, since bytes can always be
						  // converted to floats without any loss of information.
			}
		}
		bmp_in__close(&in);

		int n1 = (width - (H + S)); // floor the result, note that gradient need another sample value;
		int n2 = (height - (H + S));
		cout << "n1: " << n1 << ", n2: " << n2 << endl;

		// Allocate storage for the filtered output
		my_image_comp *output_comps = new my_image_comp[num_comps];
		my_image_comp *intermedia_comps = new my_image_comp[num_comps];
		for (n = 0; n < num_comps; n++) {
			intermedia_comps[n].init(height, width, EXTENT);
			output_comps[n].init(height, width, 0); // Don't need a border for output
		}

		// Process the image, all in floating point (easy)
		for (n = 0; n < num_comps; n++) {
			input_comps[n].perform_boundary_extension();
			intermedia_comps[n].perform_boundary_extension();
		}

		for (n = 0; n < num_comps; n++)
			gaussian_filter(input_comps + n, intermedia_comps + n, sigma, vertical);

		for (n = 0; n < num_comps; n++) {
			intermedia_comps[n].perform_boundary_extension();
			gaussian_filter(intermedia_comps + n, output_comps + n, sigma, horizontal);
		}
		delete[] intermedia_comps;

		cout << "Stride: " << output_comps->stride << endl;
		// After we get the gaussian filtered image, try to get the K[n]
		my_image_comp *matrix_comps = new my_image_comp[4];
		for (n = 0; n < 4; n++)
			matrix_comps[n].init(height, width, 0);

		for (r = H + S; r < n2; r++) {
			for (c = H + S; c < n1; c++) {
				float *op = output_comps->buf + r * output_comps->stride + c;
				//cout << " Locx " << c << "Locy " << r << " Value: " <<  op[0] << endl;
				float *mp0 = matrix_comps->buf + r * matrix_comps->stride + c;
				float *mp1 = (matrix_comps + 1)->buf + r * (matrix_comps + 1)->stride + c;
				float *mp2 = (matrix_comps + 2)->buf + r * (matrix_comps + 2)->stride + c;
				float *mp3 = (matrix_comps + 3)->buf + r * (matrix_comps + 3)->stride + c;
				float sum_11 = 0, sum_12 = 0, sum_21 = 0, sum_22 = 0;
				for (int Br = -H; Br <= H; Br++)
					for (int Bc = -H; Bc <= H; Bc++) {
						float *dp = op + Br * output_comps->stride + Bc;
						float delta_y = dp[output_comps->stride] - dp[-output_comps->stride];
						float delta_x = dp[1] - dp[-1];
						//cout << "LOC:" << "<" << (c+Bc) << "," << (r+Br) << ">" << delta_x <<"," << delta_y << endl;
						sum_11 += delta_x * delta_x;
						sum_12 += sum_21 = delta_x * delta_y;
						sum_22 += delta_y * delta_y;
						//cout << sum_11 << endl;

					}
				mp0[0] = sum_11 / 4;
				mp1[0] = sum_12 / 4;
				mp2[0] = sum_21 / 4;
				mp3[0] = sum_22 / 4;
			}
		}
		cout << "**** The matrix frames done ****" << endl;

		// Calculate K[n]
		int Kn_num = (width - 2 * (H + S))*(height - 2 * (H + S));
		float Kn_sum = 0;
		cout << " Let us get the figure of K[n]" << endl;
		my_image_comp *Kn = new my_image_comp[1];
		Kn[0].init(height, width, 0);
		for (r = H + S; r < n2; r++)
			for (c = H + S; c < n1; c++) {
				float *op = Kn->buf + r * Kn->stride + c;
				float *mp0 = matrix_comps->buf + r * matrix_comps->stride + c;
				float *mp1 = (matrix_comps + 1)->buf + r * (matrix_comps + 1)->stride + c;
				float *mp2 = (matrix_comps + 2)->buf + r * (matrix_comps + 2)->stride + c;
				float *mp3 = (matrix_comps + 3)->buf + r * (matrix_comps + 3)->stride + c;
				float det = mp0[0] * mp3[0] - mp1[0] * mp2[0];
				float trace = mp0[0] + mp3[0];
				*op = det / trace;
				Kn_sum += 
			}





		// Write the image back out again
		bmp_out out;
		if ((err_code = bmp_out__open(&out, argv[2], width, height, num_comps)) != 0)
			throw err_code;
		for (r = height - 1; r >= 0; r--)
		{ // "r" holds the true row index we are writing, since the image is
		  // written upside down in BMP files.
			for (n = 0; n < num_comps; n++)
			{
				io_byte *dst = line + n; // Points to first sample of component n
				float *src = output_comps[n].buf + r * output_comps[n].stride;
				for (int c = 0; c < width; c++, dst += num_comps) {
					if (src[c] > 255.05) {
						src[c] = 255;
					}
					else if (src[c] < 0.05) {
						src[c] = 0;
					}
					else
					{
						src[c] = int(src[c] + 0.5F);
					}
					*dst = (io_byte)src[c];
				}
			}
			bmp_out__put_line(&out, line);
		}
		bmp_out__close(&out);
		delete[] line;
		delete[] input_comps;
		delete[] output_comps;
		delete[] matrix_comps;
	}
	catch (int exc) {
		if (exc == IO_ERR_NO_FILE)
			fprintf(stderr, "Cannot open supplied input or output file.\n");
		else if (exc == IO_ERR_FILE_HEADER)
			fprintf(stderr, "Error encountered while parsing BMP file header.\n");
		else if (exc == IO_ERR_UNSUPPORTED)
			fprintf(stderr, "Input uses an unsupported BMP file format.\n  Current "
				"simple example supports only 8-bit and 24-bit data.\n");
		else if (exc == IO_ERR_FILE_TRUNC)
			fprintf(stderr, "Input or output file truncated unexpectedly.\n");
		else if (exc == IO_ERR_FILE_NOT_OPEN)
			fprintf(stderr, "Trying to access a file which is not open!(?)\n");
		return -1;
	}
	return 0;
}
