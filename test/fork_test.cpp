#include <unistd.h>
#include <stdio.h>
int main(){
    pid_t pid;
    int count=0;
    pid = fork();
    printf( "This is first time, pid = %d\n", pid );
    printf( "This is second time, pid = %d\n", pid ); 
    count++;
    printf( "count = %d\n", count );
    if ( pid>0 )
    {
        printf( "This is the parent process,the child has the pid:%d\n", pid );
    } 
    else if ( !pid )
    {
        printf( "This is the child process.\n");
    }
    else
    {
        printf( "fork failed.\n" );
    } 
        printf( "This is third time, pid = %d\n", pid );
        printf( "This is fouth time, pid = %d\n", pid );
    return 0;
    }