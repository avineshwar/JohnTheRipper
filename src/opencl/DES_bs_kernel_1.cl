/* CAUTION:Do not change or move the next 48 lines */
#define index00 31
#define index01  0
#define index02  1
#define index03  2
#define index04  3
#define index05  4
#define index06  3
#define index07  4
#define index08  5
#define index09  6
#define index10  7
#define index11  8
#define index24 15
#define index25 16
#define index26 17
#define index27 18
#define index28 19
#define index29 20
#define index30 19
#define index31 20
#define index32 21
#define index33 22
#define index34 23
#define index35 24
#define index48 63
#define index49 32
#define index50 33
#define index51 34
#define index52 35
#define index53 36
#define index54 35
#define index55 36
#define index56 37
#define index57 38
#define index58 39
#define index59 40
#define index72 47
#define index73 48
#define index74 49
#define index75 50
#define index76 51
#define index77 52
#define index78 51
#define index79 52
#define index80 53
#define index81 54
#define index82 55
#define index83 56

/*
 * This software is Copyright (c) 2012 Sayantan Datta <std2048 at gmail dot com>
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification, are permitted.
 * Based on Solar Designer implementation of DES_bs_b.c in jtr-v1.7.9
 */
#include "opencl_DES_common_kenrel.h"

#if  (HARDCODE_SALT & (!FULL_UNROLL))

#define H1_s()\
	s1(z(index00, 0), z(index01, 1), z(index02, 2), z(index03, 3), z(index04, 4), z(index05, 5),\
		B,40, 48, 54, 62);\
	s2(z(index06, 6), z(index07, 7), z(index08, 8), z(index09, 9), z(index10, 10), z(index11, 11),\
		B,44, 59, 33, 49);\
	s3(z(7, 12), z(8, 13), z(9, 14),\
		z(10, 15), z(11, 16), z(12, 17),\
		B,55, 47, 61, 37);\
	s4(z(11, 18), z(12, 19), z(13, 20),\
		z(14, 21), z(15, 22), z(16, 23),\
		B,57, 51, 41, 32);\
	s5(z(index24, 24), z(index25, 25), z(index26, 26), z(index27, 27), z(index28, 28), z(index29, 29),\
		B,39, 45, 56, 34);\
	s6(z(index30, 30), z(index31, 31), z(index32, 32), z(index33, 33), z(index34, 34), z(index35, 35),\
		B,35, 60, 42, 50);\
	s7(z(23, 36), z(24, 37), z(25, 38),\
		z(26, 39), z(27, 40), z(28, 41),\
		B,63, 43, 53, 38);\
	s8(z(27, 42), z(28, 43), z(29, 44),\
		z(30, 45), z(31, 46), z(0, 47),\
		B,36, 58, 46, 52);

#define H2_s()\
	s1(z(index48, 48), z(index49, 49), z(index50, 50), z(index51, 51), z(index52, 52), z(index53, 53),\
		B,8, 16, 22, 30);\
	s2(z(index54, 54), z(index55, 55), z(index56, 56), z(index57, 57), z(index58, 58), z(index59, 59),\
		B,12, 27, 1, 17);\
	s3(z(39, 60), z(40, 61), z(41, 62),\
		z(42, 63), z(43, 64), z(44, 65),\
		B,23, 15, 29, 5);\
	s4(z(43, 66), z(44, 67), z(45, 68),\
		z(46, 69), z(47, 70), z(48, 71),\
		B,25, 19, 9, 0);\
	s5(z(index72, 72), z(index73, 73), z(index74, 74), z(index75, 75), z(index76, 76), z(index77, 77),\
		B,7, 13, 24, 2);\
	s6(z(index78, 78), z(index79, 79), z(index80, 80), z(index81, 81), z(index82, 82), z(index83, 83),\
		B,3, 28, 10, 18);\
	s7(z(55, 84), z(56, 85), z(57, 86),\
		z(58, 87), z(59, 88), z(60, 89),\
		B,31, 11, 21, 6);\
	s8(z(59, 90), z(60, 91), z(61, 92),\
		z(62, 93), z(63, 94), z(32, 95),\
		B,4, 26, 14, 20);

#define H2_k48()\
	s1(y48(index48, 12), y48(index49, 46), y48(index50, 33), y48(index51, 52), y48(index52, 48), y48(index53, 20),\
		B,8, 16, 22, 30);\
	s2(y48(index54, 34), y48(index55, 55), y48(index56, 5), y48(index57, 13), y48(index58, 18), y48(index59, 40),\
		B,12, 27, 1, 17);\
	s3(y48(39, 4), y48(40, 32), y48(41, 26),\
		y48(42, 27), y48(43, 38), y48(44, 54),\
		B,23, 15, 29, 5);\
	s4(y48(43, 53), y48(44, 6), y48(45, 31),\
		y48(46, 25), y48(47, 19), y48(48, 41),\
		B,25, 19, 9, 0);\
	s5(y48(index72, 15), y48(index73, 24), y48(index74, 28), y48(index75, 43), y48(index76, 30), y48(index77, 3),\
		B,7, 13, 24, 2);\
	s6(y48(index78, 35), y48(index79, 22), y48(index80, 2), y48(index81, 44), y48(index82, 14), y48(index83, 23),\
		B,3, 28, 10, 18);\
	s7(y48(55, 51), y48(56, 16), y48(57, 29),\
		y48(58, 49), y48(59, 7), y48(60, 17),\
		B,31, 11, 21, 6);\
	s8(y48(59, 37), y48(60, 8), y48(61, 9),\
		y48(62, 50), y48(63, 42), y48(32, 21),\
		B,4, 26, 14, 20);

#ifndef RV7xx
#define x(p) vxorf(B[index96[p]], _local_K[_local_index768[p + k] + local_offset_K])
#define z(p, q) vxorf(B[p]      , _local_K[ *_index768_ptr++ + local_offset_K])
#else
#define x(p) vxorf(B[index96[p]], _local_K[index768[p + k] + local_offset_K])
#define z(p, q) vxorf(B[p]      , _local_K[index768[q + k] + local_offset_K])
#endif

#define y48(p, q) vxorf(B[p]     , _local_K[q + local_offset_K])

void des_loop(__private vtype *B,
	      __local DES_bs_vector *_local_K,
	      __local ushort *_local_index768,
	      constant uint *index768,
	      unsigned int iterations,
	      unsigned int local_offset_K) {

		int k = 0, i, rounds_and_swapped;

		for (i = 0; i < 64; i++)
			B[i] = 0;

		k=0;
		rounds_and_swapped = 8;

#ifndef RV7xx
		__local ushort *_index768_ptr ;
#endif

start:
#ifndef RV7xx
		_index768_ptr = _local_index768 + k ;
#endif
		H1_s();
		if (rounds_and_swapped == 0x100) goto next;
		H2_s();
		k +=96;
		rounds_and_swapped--;

		if (rounds_and_swapped > 0) goto start;
		k -= (0x300 + 48);
		rounds_and_swapped = 0x108;
		if (--iterations) goto swap;

		return;

swap:
		H2_k48();
		k += 96;
		if (--rounds_and_swapped) goto start;

next:
		k -= (0x300 - 48);
		rounds_and_swapped = 8;
		iterations--;
		goto start;

}

__kernel void DES_bs_25_self_test( constant uint *index768 __attribute__((max_constant_size(3072))),
			  __global DES_bs_transfer *DES_bs_all,
			  __global DES_bs_vector *B_global) {

		unsigned int section = get_global_id(0), global_offset_B, local_offset_K;
		unsigned int local_id = get_local_id(0);
		int iterations;

		global_offset_B = 64 * section;
		local_offset_K  = 56 * local_id;

		vtype B[64], i;

		__local DES_bs_vector _local_K[56*WORK_GROUP_SIZE] ;
		__local ushort _local_index768[768] ;

		if (!local_id )
			for (i = 0; i < 768; i++)
				_local_index768[i] = index768[i];

		barrier(CLK_LOCAL_MEM_FENCE);

		DES_bs_finalize_keys_bench(section, DES_bs_all, local_offset_K, _local_K);
		iterations = 25;
		des_loop(B, _local_K, _local_index768, index768, iterations, local_offset_K);
		for (i = 0; i < 64; i++)
			B_global[global_offset_B + i] = (DES_bs_vector)B[i] ;
}

__kernel void DES_bs_25_mm( constant uint *index768 __attribute__((max_constant_size(3072))),
			  __global DES_bs_vector *B_global,
			  __global int *binary,
			  int num_loaded_hash,
			  __global char *transfer_keys,
			  __global struct mask_context *msk_ctx,
			  __global uint *outKeyIdx) {

		unsigned int section = get_global_id(0), local_offset_K, loop_count;
		unsigned int local_id = get_local_id(0),  activeRangeCount, offset;
		unsigned char input_key[8], activeRangePos[8], rangeNumChars[3], start[3];

		local_offset_K  = 56 * local_id;

		vtype B[64], i;

		__local DES_bs_vector _local_K[56*WORK_GROUP_SIZE] ;
		__local ushort _local_index768[768] ;
		__local unsigned char range[3*MAX_CHARS];

		int iterations;

		if(!section)
			for(i = 0; i < num_loaded_hash; i++)
				outKeyIdx[i] = outKeyIdx[i + num_loaded_hash] = 0;

		for (i = 0; i < 8 ;i++)
			activeRangePos[i] = msk_ctx[0].activeRangePos[i];

		activeRangeCount = msk_ctx[0].count;

		for (i = 0; i < 8; i++ )
			input_key[i] = transfer_keys[8*section + i];

		for (i = 0; i < 3; i++) {
			rangeNumChars[i] = msk_ctx[0].ranges[activeRangePos[i]].count;
			start[i] = msk_ctx[0].ranges[activeRangePos[i]].start;
		}

		loop_count = 1;
		for(i = 0; i < activeRangeCount; i++)
				loop_count *= rangeNumChars[i];

		loop_count = loop_count&31 ? (loop_count >> 5) + 1: loop_count >> 5;

		if(!section)
			for(i = 0; i < num_loaded_hash; i++)
				outKeyIdx[i] = outKeyIdx[i + num_loaded_hash] = 0;
		barrier(CLK_GLOBAL_MEM_FENCE);

		if (!local_id ) {
			for (i = 0; i < 768; i++)
				_local_index768[i] = index768[i];

			for (i = 0; i < MAX_CHARS; i++) {
				range[i] = msk_ctx[0].ranges[activeRangePos[0]].chars[i];
				range[i + MAX_CHARS] = msk_ctx[0].ranges[activeRangePos[1]].chars[i];
				range[i + 2*MAX_CHARS] = msk_ctx[0].ranges[activeRangePos[2]].chars[i];
			}
		}

		barrier(CLK_LOCAL_MEM_FENCE);

		DES_bs_finalize_keys_passive(local_offset_K, _local_K, activeRangePos, activeRangeCount, input_key);

		offset =0;
		i = 1;
		for (i = 1; i <= loop_count; i++) {

			DES_bs_finalize_keys_active(local_offset_K, _local_K, offset, activeRangePos, activeRangeCount, range, rangeNumChars, input_key, start);

			iterations = 25;
			des_loop(B, _local_K, _local_index768, index768, iterations, local_offset_K);

			cmp_s( B, binary, num_loaded_hash, B_global, offset, outKeyIdx, section);

			offset = i*32;

		} ;
}

#if !MASK_MODE_ONLY
__kernel void DES_bs_25_om( constant uint *index768 __attribute__((max_constant_size(3072))),
			  __global DES_bs_transfer *DES_bs_all,
			  __global DES_bs_vector *B_global,
			  __global int *binary,
			  int num_loaded_hash,
			  __global uint *outKeyIdx) {

		unsigned int section = get_global_id(0), global_offset_B, local_offset_K;
		unsigned int local_id = get_local_id(0);
		int iterations;

		global_offset_B = 64 * section;
		local_offset_K  = 56 * local_id;

		vtype B[64], i;

		__local DES_bs_vector _local_K[56*WORK_GROUP_SIZE] ;
		__local ushort _local_index768[768] ;

		if(!section)
			for(i = 0; i < num_loaded_hash; i++)
				outKeyIdx[i] = outKeyIdx[i + num_loaded_hash] = 0;
		barrier(CLK_GLOBAL_MEM_FENCE);

		if (!local_id )
			for (i = 0; i < 768; i++)
				_local_index768[i] = index768[i];

		barrier(CLK_LOCAL_MEM_FENCE);

		DES_bs_finalize_keys_bench(section, DES_bs_all, local_offset_K, _local_K);
		iterations = 25;
		des_loop(B, _local_K, _local_index768, index768, iterations, local_offset_K);
		cmp_s( B, binary, num_loaded_hash, B_global, 0, outKeyIdx, section);

}
#endif

#endif