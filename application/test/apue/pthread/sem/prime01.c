#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "sem.h"


#define START 20000
#define END   20200
void * thr_fun(void * arg)
{
	int n = (int)arg;	
	int i, flag = 1;
	for(i=2; i<n; i++){
		if(n % i == 0){
			flag = 0;
			break;
		}
	}
	if(flag == 1)
		printf("%d\n", n);
	sem_op(1);
	pthread_exit(NULL);
}
int
main(void)
{
	int i, ret;
	sem_init(4);
	pthread_t tid[END-START+1];
	for(i=0; i<END-START+1; i++){
		sem_op(-1);
		ret = pthread_create(&tid[i], NULL, thr_fun, (void*)(i+START));
		if(ret != 0){
			fprintf(stderr, "pthread_create():%s\n", strerror(ret));
			exit(-1);
		}
	}
	for(i=0; i<END-START+1; i++){
		pthread_join(tid[i], NULL);
	}
	return 0;
}
