#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <pthread.h>


#define RC_LEN 100 // length of each records

// struct used to denote the map
struct map {
    int *keys;
    int **followings;
    int length;
};

// strcture passed into the thread function
struct arrsects{
    int section; // which section of the arrary it is 
    int l; // low index
    int h; // high index
    int num_level_thread; // total number of threads of current level
};

struct readinarg {
    int section;
    int *ptr;
    int* keys_arr;
    int** following_darr;
    int start;
    int radius;
    int num_threads;
};

struct map myMap; // global myMap variable
int current_finished_threads;
int readin_finished_threads;
// mutex: wait for current level child threads
pthread_cond_t condition_wait = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Print the content of the map, used for debugging
*/
void 
printMap(struct map * myMap) {

    printf("psort.map(\n");
    int length = myMap->length;
    printf("---length %d---,\n", length);
    for (int i = 0; i < length; i ++)
    {
        int* followings = myMap->followings[i];
        printf("%dth key: %d, following[0], [1], [2]: %d, %d, %d,\n", i, myMap->keys[i], followings[0], followings[1], followings[2]);
    }
    printf(")\n");
}

void 
freeMap(struct map * myMap) {
    int length = myMap->length;
    free(myMap->keys);
    for(int i = 0; i < length; i ++)
        free(myMap->followings[i]);
    free(myMap->followings);
}

/**
 * Helper
 * Print the error message
*/
void 
printErrMsg(char* msg) {
    printf("%s\n", msg);
    exit(1);
}

void* readin_helper(void* args)
{
    struct readinarg* argptr = (struct readinarg*) args;
    //int section = argptr->section;
    int *ptr = argptr->ptr;
    int* keys_arr = argptr->keys_arr;
    int** following_darr = argptr->following_darr;
    int start = argptr->start;
    int radius = argptr->radius;
    int num_threads = argptr->num_threads;
    
    for(int i = 0; i < radius; i ++)
    {
        int key = ptr[start + i * 25];
        keys_arr[start / 25 + i] = key;
        int* following = malloc(24 * sizeof(int));
        for (int j = 0; j < 96/sizeof(int); j ++) {
            following[j] = ptr[start + i * 25 + j + 1];
        }
        following_darr[start / 25 + i] = following;
    }
    pthread_mutex_lock(&lock);
    readin_finished_threads++;
    // printf("now finished: %d, total: %d\n", readin_finished_threads, num_threads);
    if (readin_finished_threads >= num_threads)
        pthread_cond_signal(&condition_wait);
    pthread_mutex_unlock(&lock);
    return NULL;
}

void 
readin(const char* filename)
{
    // open the file
    int fd = open(filename, O_RDONLY);
    if(fd == -1){ // safety reasons
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    
    // map file to memory, and read
    struct stat buffer;
    fstat(fd, &buffer);
    int size = buffer.st_size;
    if(buffer.st_size <= 0){
        fprintf(stderr, "An error has occurred\n");
        close(fd); 
        exit(0);
    }
    int *ptr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    
    // read the file
    int rc_no = size/RC_LEN + 1;
    int* keys_arr = malloc(rc_no * sizeof(int)); // each 4-byte integer
    int** following_darr = malloc(rc_no * sizeof(int*)); // each 96-byte followings
    
    int num_thread = get_nprocs();
    int num_chunk = size / 100;
    num_thread = num_thread > num_chunk ? num_chunk : num_thread;
    int remain_block = (size / 100) % num_thread;
    int radius = (size / 100) / num_thread;

    pthread_t threads[num_thread];                       // thread lists
    struct readinarg args_arr[num_thread];   

    for(int i = 0; i < num_thread; i ++)
    {
        args_arr[i].section = i;
        args_arr[i].ptr = ptr;
        args_arr[i].keys_arr = keys_arr;
        args_arr[i].following_darr = following_darr;
        args_arr[i].start = i * 25 * radius;
        args_arr[i].radius = radius;
        args_arr[i].num_threads = num_thread;
        int rc = pthread_create(&threads[i], NULL, readin_helper, (void*)(&args_arr[i]));
        if(rc) {
            printErrMsg("Error while creating threads");
            exit(1);
        }
    }
    pthread_mutex_lock(&lock);
    while (readin_finished_threads < num_thread)
        pthread_cond_wait(&condition_wait, &lock);
    pthread_mutex_unlock(&lock);
    if (remain_block > 0) {
        int start_point = radius * num_thread * 25;
        int start_index = radius * num_thread;
        for(int i = 0; i < remain_block; i ++)
        {
            int key = ptr[start_point + i * 25];
            keys_arr[start_index + i] = key;
            int* following = malloc(24 * sizeof(int));
            for (int j = 0; j < 96/sizeof(int); j ++) {
                following[j] = ptr[start_point + i * 25 + j + 1];
            }
            following_darr[start_index + i] = following;
        }
    }
    
    myMap.length = size / 100;
    myMap.keys = keys_arr;
    myMap.followings = following_darr;
    munmap(ptr, size);
    // printf("step 2\n");
}

void writeOut(const char* filename, struct map *myMap) {
    FILE *fp;
    fp = fopen(filename, "w");
    // int file = fileno(fp);
    int length = myMap->length;
    int *copy = malloc(length * 25 * sizeof(int));
    for (int i = 0; i < length * 25; i ++)
    {
        //printf("i : %d; \n", i);
        if (i % 25 == 0)
        {
            copy[i] = myMap->keys[i / 25];
            continue;
        }
        if (i % 25 == 1)
        {
            for(int j = 0; j < 24; j ++)
                copy[i + j] = myMap->followings[i / 25][j];
            i += 23;
        }
    }
    fwrite(copy, 4, length * 25, fp);
    fflush(fp);
    fclose(fp);
    free(copy);
}

// Divide and Conquer method in parallelism

void 
merge(struct map * myMap, int left, int mid, int right, int num_level_threads) {
    int left_current = left;
    int right_current = mid + 1;

    int *temp = malloc(sizeof(int) * (right - left + 1));
    int **temp_following = malloc(sizeof(int*) * (right - left + 1));
    int current_index = 0;

    // 神的代碼
    while(left_current <= mid && right_current <= right) {
        //printf("myMap->keys[left_current]: %d\n", myMap->keys[left_current]);
        //printf("myMap->keys[right_current]: %d\n", myMap->keys[right_current]);

        temp[current_index] = myMap->keys[left_current] < myMap->keys[right_current] ? myMap->keys[left_current] : myMap->keys[right_current];
        temp_following[current_index++] = myMap->keys[left_current] < myMap->keys[right_current] ? myMap->followings[left_current++] : myMap->followings[right_current++];
    }

    while(left_current <= mid) {
        temp[current_index] = myMap->keys[left_current];
        temp_following[current_index++] = myMap->followings[left_current++];
    }

    while(right_current <= right) {
        temp[current_index] = myMap->keys[right_current];
        temp_following[current_index++] = myMap->followings[right_current++];
    }
    int copyIndex = 0;
    while (left <= right)
    {
        //printf("temp[copyIndex] %d\n", temp[copyIndex]);
        myMap->keys[left] = temp[copyIndex];
        //printf("myMap->keys[left]: %d\n", myMap->keys[left]);
        myMap->followings[left++] = temp_following[copyIndex++];
    }
    pthread_mutex_lock(&lock);
    current_finished_threads++;
    
    if (current_finished_threads >= num_level_threads)
        pthread_cond_signal(&condition_wait);
    pthread_mutex_unlock(&lock);
    free(temp);
    free(temp_following);
}

void 
merge2(struct map * myMap, int left, int mid, int right) {
    int left_current = left;
    int right_current = mid + 1;

    int *temp = malloc(sizeof(int) * (right - left + 1));
    int **temp_following = malloc(sizeof(int*) * (right - left + 1));
    if (temp == NULL || temp_following == NULL)
    {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    int current_index = 0;

    // 神的代碼
    while(left_current <= mid && right_current <= right) {
        //printf("myMap->keys[left_current]: %d\n", myMap->keys[left_current]);
        //printf("myMap->keys[right_current]: %d\n", myMap->keys[right_current]);

        temp[current_index] = myMap->keys[left_current] < myMap->keys[right_current] ? myMap->keys[left_current] : myMap->keys[right_current];
        temp_following[current_index++] = myMap->keys[left_current] < myMap->keys[right_current] ? myMap->followings[left_current++] : myMap->followings[right_current++];
    }

    while(left_current <= mid) {
        temp[current_index] = myMap->keys[left_current];
        temp_following[current_index++] = myMap->followings[left_current++];
    }

    while(right_current <= right) {
        temp[current_index] = myMap->keys[right_current];
        temp_following[current_index++] = myMap->followings[right_current++];
    }
    int copyIndex = 0;
    while (left <= right)
    {
        //printf("temp[copyIndex] %d\n", temp[copyIndex]);
        myMap->keys[left] = temp[copyIndex];
        //printf("myMap->keys[left]: %d\n", myMap->keys[left]);
        myMap->followings[left++] = temp_following[copyIndex++];
    }
    free(temp);
    free(temp_following);
}

void 
mergeDivide(struct map * myMap, int left, int right) {
    if (left >= right)
        return ;
    int mid = (left + right) / 2;
    mergeDivide(myMap, left, mid);
    mergeDivide(myMap, mid + 1, right);
    merge2(myMap, left, mid, right);
    //printf("after merge: map: \n");
    //printMap(myMap);
}

void*
thread_merge_sort(void* arg){
    // determine parts of the array
    struct arrsects * arg_sect = (struct arrsects *)arg;
    int left = arg_sect->l;
    int right = arg_sect->h;
    // believe in mr. ye
    // mergeDivide(&myMap, left, right);
    merge(&myMap, left, (left + right) / 2, right, arg_sect->num_level_thread);
    return NULL;
}

void*
thread_merge_divide(void* arg){
    // determine parts of the array
    struct arrsects * arg_sect = (struct arrsects *)arg;
    int left = arg_sect->l;
    int right = arg_sect->h;
    // believe in mr. ye
    mergeDivide(&myMap, left, right);
    pthread_mutex_lock(&lock);
    current_finished_threads++;
    // printf("current finished num:%d\n", current_finished_threads);
    if (current_finished_threads >= arg_sect->num_level_thread)
        pthread_cond_signal(&condition_wait);
    pthread_mutex_unlock(&lock);
    return NULL;
}

void mt_thread_sort() {
    // concurrent speed-up
    const int processor_no = get_nprocs();
    const int thread_no = processor_no > myMap.length ? myMap.length : processor_no; 
    int radius = myMap.length % thread_no > 0 ?  myMap.length / thread_no + 1 : myMap.length / thread_no;           // elements per thread
    int initrun = 1;
    int merge_smaller = 0;
    int last_round = 0;

    // radius size -> begin with epthread, * 2 each time, eventually come to myMap.length
    // imagine first radius = 6 (length = 20, thread_no = 3)

    pthread_t threads[thread_no];                       // thread lists
    struct arrsects args_arr[thread_no];             //arrsection arg wrapper

    // @TODO: merge the consequtive four pieces of arrays 
    while(radius < myMap.length) { // merge (thread_no - 1) times
        // int last_round = radius * 2 >= myMap.length ? 1 : 0;
        int num_pieces = myMap.length / radius;
        int num_threads = num_pieces > myMap.length ? myMap.length : num_pieces;
        // int last_alone = num_threads % 2 == 1;
        current_finished_threads = 0; // init current level

        for(int i = 0; i < num_threads; i++) {
                args_arr[i].section = i;
                args_arr[i].l = i * radius;
                args_arr[i].num_level_thread = num_threads;
                args_arr[i].h = (i + 1) * radius - 1;
                int rc;
                if (initrun == 1)
                    rc = pthread_create(&threads[i], NULL, thread_merge_divide, (void*)(&args_arr[i]));
                else
                    rc = pthread_create(&threads[i], NULL, thread_merge_sort, (void*)(&args_arr[i]));
                if(rc){
                    printErrMsg("Error while creating threads");
                    exit(1);
                }
        }
        pthread_mutex_lock(&lock);
        while (current_finished_threads < num_threads)
            pthread_cond_wait(&condition_wait, &lock);
        pthread_mutex_unlock(&lock);
        // printMap(&myMap);
        if (initrun == 1 && myMap.length % radius > 0)
        {
            initrun = 0;
            mergeDivide(&myMap, (myMap.length / radius) * radius, myMap.length - 1);
        }
        last_round = (myMap.length % radius > 0 && radius * 2 >= myMap.length) ? 1 : 0;
        merge_smaller = (initrun == 0 && myMap.length % radius > 0) ? 1 : 0;
        if (merge_smaller == 1) {
            merge2(&myMap, (myMap.length / radius - 1) * radius, (myMap.length / radius) * radius - 1, myMap.length - 1);
        }
        radius = radius * 2;
        
    }
    if (last_round == 1)
        merge2(&myMap, 0, 0 + (radius / 2 - 1), myMap.length - 1);
}

int 
main(int argc, char const *argv[])
{
    const char* filename = argv[1];
    //const char* filename = "inputfiles/9.in";
    const char* output1 = argv[2];
    //const char* output1 = "inputfiles/8.out";

    //initialize mymap
    readin(filename);
    //printMap(&myMap);
    //printf("之后\n");
    mt_thread_sort();
    writeOut(output1, &myMap);
    //printMap(&myMap);
    return 0;
}
