#pragma once

#define da_append(da, item)\
	do {\
		if ((da)->count >= (da)->capacity) {\
			if ((da)->capacity == 0) (da)->capacity = 8;\
			(da)->capacity *= 2;\
			(da)->items = realloc((da)->items, (da)->capacity * sizeof((item)));\
		}\
		(da)->items[(da)->count] = (item);\
		(da)->count += 1;\
	} while (0)
