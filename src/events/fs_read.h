#pragma once
void read_from_file(char *filename, uint64_t offset, void *callback, void *data);
void read_whole_file(char *filename, void *callback, void *data);
