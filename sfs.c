/**
* CS 4348.003 Project 4 
* Anthony Iorio ati140030
* Lucas Castro ldc140030
* 
* Simple File System: Simple File System
**/


#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "disk.h"
#include "sfs.h"

int fd = 0;
int i = 0;
int curFile = 0;
int curBlock = 0;
int nextBlock = 0;
directory dirEntries[MAX_FILES];	// array of directory entries

// create and open disk
int make_sfs(char *disk_name){
	strcpy(superBlock, "super block data\0");

	if ((create_disk(disk_name, MAX_BLOCKS*BLOCK_SIZE)) < 0)		// create disk
		return -1;

	if ((fd = open_disk(disk_name)) < 0)							// open disk
		return -1;

	fillFAT();														// fill FAT control blocks

	write_block(fd, 0, superBlock);					// write super block to disk
	write_block(fd, 1, (char*)FAT);					// write FAT to disk
	// write_block(fd, 5, (char*)dirEntries);		// write directory to disk

	close_disk(fd);									// close disk
	return 0;
} 

// mount disk
int mount_sfs(char *disk_name){
	if ((fd = open_disk(disk_name)) < 0)			// open disk
		return -1;

	fdNameTable[fd] = disk_name; 
	
	read_block(fd, 0, superBlock);					// read super block from disk into local memory
	read_block(fd, 1, (char*)FAT);					// read FAT from disk into local memory
	// read_block(fd, 5, (char*)dirEntries);		// read FAT from disk into local memory
	return 0;
}

// unmount disk
int unmount_sfs(char *disk_name){

	for (i = 0; i < MAX_OPEN_FILES; i++)
		if (fdNameTable[i])
			fd = i;

	write_block(fd, 0, (char*)superBlock);					// update FAT on disk
	for (i = 1; i < 5; i++)
		write_block(fd, i, (char*)FAT);					// update FAT on disk
	// write_block(fd, 5, (char*)dirEntries);					// update FAT on disk
	
	close_disk(fd);
	printf("%s(%d) unmounted .\n", disk_name, fd);
	return 0;
}

// open file
int sfs_open(char *file_name){
	char name[FNAME_LENGTH];
	if (directoryCount() > MAX_FILES)								// max open file check
		return -1;

	if ((fd = open(file_name, O_RDWR, (mode_t)0777)) < 0){		// open file
	    printf("error: %s\n", strerror(errno));
		return -1;
	}

	fdNameTable[fd] = file_name;
	fdPointerTable[fd] = 0;
	curFile = getDirIndexFromName(file_name);						// get correct file directory from name
	dirEntries[curFile].numInstances++;

	printf("%s(%d) opened.\n", dirEntries[curFile].name, fd);

	return fd;
}


// close file
int sfs_close(int fd){

	if (close(fd) < 0)
		return -1;

	curFile = getDirIndexFromName(fdNameTable[fd]);			// get correct file directory from name
	dirEntries[curFile].numInstances--;					// decrement number of open instances of specific file

	fdNameTable[fd] = NULL;									// remove fd from fdNameTable
	fdPointerTable[fd] = -1;

	printf("%s(%d) closed.\n", dirEntries[curFile].name, fd);
	return 0;
}

// create file
int sfs_create(char *file_name){

	curFile = getFreeDirectory();										// get correct file directory from name

	if (curFile > MAX_FILES)													// max number of files check
		return -1;

	if ((fd = open(file_name, O_RDWR | O_CREAT | O_EXCL, (mode_t)0777)) < 0)	// opens file
		return -1;

	strcpy(dirEntries[curFile].name, file_name);								// update directory strcture information
	dirEntries[curFile].size = 0;
	dirEntries[curFile].firstBlock = 0;
	printf("%s(%d) created.\n", dirEntries[curFile].name, fd);

	close(fd);																	// close file
	return 0;
}


// delete file
int sfs_delete(char *file_name){	
	
	if(access(file_name, F_OK) < 0){									// existence check
    	printf("Delete on %s failed. File does not exist.\n", file_name);
    	return -1;
	}

	curFile = getDirIndexFromName(file_name);

	if (dirEntries[curFile].numInstances > 0){							// multiple instances open
		printf("Delete on %s failed. Open instances: %d\n", file_name, dirEntries[curFile].numInstances);
		return -1;
	} else{																// no instances open
		unlink(file_name);												// delete file
	
		curBlock = dirEntries[curFile].firstBlock; 
		nextBlock = curBlock;
		// printf("curblock: %d\n", curBlock);
		while (FAT[curBlock] > 0){
			nextBlock = FAT[curBlock];
			FAT[curBlock] = 0;
			curBlock = nextBlock;												// free block
		}

		if (FAT[curBlock] == -1)
			FAT[curBlock] = 0;
			

		strcpy(dirEntries[curFile].name, "");					// clean dir array
		dirEntries[curFile].size = 0;
		dirEntries[curFile].firstBlock = 0;
		dirEntries[curFile].numInstances = 0;

		printf("%s deleted.\n", file_name);
	}
	return 0;
}

// write to a file
// FAT doesnt account for multiple writes to the same file properly
int sfs_write(int fd, void *buf, size_t count){
	int numBlocks = (count / BLOCK_SIZE) - 1;

	if ((curBlock = getFreeBlock()) == -1337){							// disk full
		printf("Write Error. Disk Full.\n");
		return 0;
	}

	dirEntries[curFile].firstBlock = curBlock;				// **possible issue
	curFile = getDirIndexFromName(fdNameTable[fd]);							// find file in directory

	for (numBlocks; numBlocks >= 0; numBlocks--){
		if ((write_block(fd, (fdPointerTable[fd] / BLOCK_SIZE), (char*)buf)) < 0)			// write a block
			return -1;

		FAT[curBlock] = -1;											// block written									

		if (numBlocks > 0){
			if ((nextBlock = getFreeBlock()) == -1337){			// disk full check
				printf("Write Error. Disk Full.\n");
				return 0;
			} else
				FAT[curBlock] = nextBlock;						// update FAT
		}
		curBlock = nextBlock;
		fdPointerTable[fd] += BLOCK_SIZE;
	}
	dirEntries[curFile].size += count;
	FAT[curBlock] = -1;

	return (int)count;
}

// read file
int sfs_read(int fd, void *buf, size_t count){
	int numBlocks = (count / BLOCK_SIZE) - 1;
	size_t bytesToRead = count;

	curBlock = dirEntries[curFile].firstBlock;	
	curFile = getDirIndexFromName(fdNameTable[fd]);							// find file in directory

	for (numBlocks; numBlocks >= 0; numBlocks--){
		if ((read_block(fd, (fdPointerTable[fd] / BLOCK_SIZE), (char*)buf)) < 0)			// write a block
			return -1;

		bytesToRead -= BLOCK_SIZE;									// less bytes to write
		nextBlock = curBlock;
		while (FAT[curBlock] > 0){
			nextBlock = FAT[curBlock];
			curBlock = nextBlock;												// free block
		}

		fdPointerTable[fd] += lseek(fd, BLOCK_SIZE, SEEK_SET);
	}

	return (int)count;
}

// seek through file
int sfs_seek(int fd, int offset){
	curFile = getDirIndexFromName(fdNameTable[fd]);					// find file in directory

	if (offset > dirEntries[curFile].size)						// offset larger than file
		return -1;

	fdPointerTable[fd] = lseek(fd, offset, SEEK_SET);							// seek

	return 0;
}




// helper functions
int getFreeBlock(){
	for (i=FIRST_DATA_BLOCK; i < MAX_BLOCKS; i++){									// find first free block
		if (FAT[i] == 0)
			return i;
	}
	return -1337;														// disk full
}

void printFAT(){
	printf("**Printing FAT:\n");
	for (i=0; i < MAX_BLOCKS; i++)										// find first free block
		if (FAT[i] != 0)
			printf("\tFAT[%d]: %d\n", i, FAT[i]);
}

int getDirIndexFromName(char *name){
	for (i = 0; i < MAX_FILES; i++)
		if (strcmp(dirEntries[i].name, name) == 0)
			return i;
}

void printDirectory(){
	printf("**Printing Directory:\n");
	for (i = 0; i < MAX_FILES; i++)
		if (strcmp(dirEntries[i].name, "") != 0)
			printf("\tdir[%d]. name: %s. size: %d\n", i, dirEntries[i].name, dirEntries[i].size);
}

int directoryCount(){
	int count = 0;
	for (i = 0; i < MAX_FILES; i++)
		if (strcmp(dirEntries[i].name, "") != 0)
			count++;

	return count;
}

int getFreeDirectory(){
	for (i = 0; i < MAX_FILES; i++)
		if (strcmp(dirEntries[i].name, "") == 0)
			return i;
}

void printfdNameTable(){
	printf("**Printing FD Table:\n");
	for (i = 0; i < MAX_FILES; i++)
		if (fdNameTable[i])
			printf("\tfdNameTable[%d]: %s\n", i, fdNameTable[i]);
}

// it looks nicer down here
void fillFAT(){
	for (i=0; i < BLOCK_SIZE; i++){
		switch(i){
			case 0:
				FAT[i] = -1;
			 	break;
			 case 1:
			 	FAT[i] = 2;
			 	break;
			 case 2:
			 	FAT[i] = 3;
			 	break;
			 case 3:
			 	FAT[i] = 4;
			 	break;
			 case 4:
			 	FAT[i] = -1;
			 	break;
			 case 5:
			 	FAT[i] = -1;
			 	break;
			 default:
			 	FAT[i] = 0;
			 	break;
		}
	}
}