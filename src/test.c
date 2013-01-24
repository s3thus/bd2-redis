#include "fmacros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include "hiredis.h"

enum connection_type
{
    CONN_TCP,
    CONN_UNIX
};

struct config
{
    enum connection_type type;

    struct _tcp
    {
        const char *host;
        int port;
    } tcp;

    struct _sock
    {
        const char *path;
    } sock;
};

char** keys=NULL;
int num;
char* filename="keys_1000.txt";

static long long usec(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

static redisContext *select_database(redisContext *c)
{
    redisReply *reply;

    /* Switch to DB 9 for testing, now that we know we can chat. */
    reply = redisCommand(c,"SELECT 9");
    assert(reply != NULL);
    freeReplyObject(reply);

    /* Make sure the DB is emtpy */
    reply = redisCommand(c,"DBSIZE");
    assert(reply != NULL);
    if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0)
    {
        /* Awesome, DB 9 is empty and we can continue. */
        freeReplyObject(reply);
    }
    else
    {
        //printf("Database #9 is not empty, test can not continue\n");
        //exit(1);
        reply = redisCommand(c,"FLUSHDB");
        assert(reply != NULL);
        freeReplyObject(reply);
    }

    return c;
}

static void disconnect(redisContext *c) {
    redisReply *reply;

    /* Make sure we're on DB 9. */
    reply = redisCommand(c,"SELECT 9");
    assert(reply != NULL);
    freeReplyObject(reply);
    reply = redisCommand(c,"FLUSHDB");
    assert(reply != NULL);
    freeReplyObject(reply);

    /* Free the context as well. */
    redisFree(c);
}

static redisContext *connect(struct config config)
{
    redisContext *c = NULL;

    if (config.type == CONN_TCP)
    {
        c = redisConnect(config.tcp.host, config.tcp.port);
    }
    else if (config.type == CONN_UNIX)
    {
        c = redisConnectUnix(config.sock.path);
    }
    else
    {
        assert(NULL);
    }

    if (c->err)
    {
        printf("Connection error: %s\n", c->errstr);
        exit(1);
    }

    return select_database(c);
}

static void read_file(char* name)
{
    FILE* file;
    int i;
    char line[25]="";
    char command[30]="";
    
    file = fopen (name, "rt");
    fgets(line, 25, file);
    num=atoi(line);
    keys = malloc (num * sizeof(char*));
    
    for(i=0; i<num; i++)
    {
        keys[i] = malloc(80*sizeof(char));
        strcpy(keys[i],"SET ");
    }
    
    i=0;
    while(fgets(line, 25, file) != NULL)
    {
        strcat(keys[i], line);
        if(keys[i][strlen(keys[i])-2]=='\r')
            keys[i][strlen(keys[i])-2] = '\0';
        //printf("%s\n", keys[i]);
        ++i;
    }
    
    fclose(file);
}

static void test_data(struct config config)
{
    redisContext *c = connect(config);
    redisReply **replies;
    int i;
    long long t1, t2;

    replies = malloc(sizeof(redisReply*)*num);
    printf("Adding keys to database... ");
    t1 = usec();
    for (i = 0; i < num; i++)
    {
        //printf("Adding key(%d) to database... ", i);
        replies[i] = redisCommand(c,keys[i]);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_STATUS);
        //printf("Done!\n");
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("Task of %dx SET done in %.3fs\n", num, (t2-t1)/1000000.0);

    disconnect(c);
}

void freeMem(void)
{
    int i;
    for(i=0; i<num;i++)
        free(keys[i]);
    free(keys);
    keys=NULL;
}

int main(int argc, char **argv)
{
    atexit(freeMem);
    struct config cfg = {
        .tcp = {
            //.host = "127.0.0.1",
            .host = "s3thus.dyndns.org",
            //.host = "192.168.56.1",
            .port = 6379
        },
        .sock = {
            .path = "/tmp/redis.sock"
        }
    };

    /* Parse command line options. */
    argv++; argc--;
    while (argc)
    {
        if (argc >= 2 && !strcmp(argv[0],"-h"))
        {
            argv++; argc--;
            cfg.tcp.host = argv[0];
        }
        else if (argc >= 2 && !strcmp(argv[0],"-p"))
        {
            argv++; argc--;
            cfg.tcp.port = atoi(argv[0]);
        }
        else if (argc >= 2 && !strcmp(argv[0],"-f"))
        {
            argv++; argc--;
            filename = argv[0];
        }
        else
        {
            fprintf(stderr, "Invalid argument: %s\n", argv[0]);
            exit(1);
        }
        argv++; argc--;
    }

    cfg.type = CONN_TCP;
    read_file(filename);
    test_data(cfg);
    return 0;
}
