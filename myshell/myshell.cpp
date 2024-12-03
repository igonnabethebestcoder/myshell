#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <fcntl.h>

//输入缓冲区最大限制
#define BUF_SIZE 1024

//最多参数数量
#define PARAMETER_SIZE 32

//fd数量上限
#define MAX_FD 1024

//重定向
#define REDIR_NONE 0B0001 //没有重定向
#define REDIR_INPUT 0B0010 //输入重定向
#define REDIR_OUTPUT 0B0100 //输出重定向
#define REDIR_APPEND 0B1000 //追加重定向

// 内置的状态码
enum {
    RESULT_NORMAL,//正常
    ERROR_FORK, //fork异常
    ERROR_COMMAND, //命令不存在
    ERROR_WRONG_PARAMETER, //错误的参数
    ERROR_MISS_PARAMETER, //缺失参数
    ERROR_TOO_MANY_PARAMETER, //过多参数
    ERROR_CD, //cd失败
    ERROR_SYSTEM, //系统错误
    ERROR_MEMORY_ALLOCATION, //内存非配错误
    ERROR_EXIT, //退出错误
    ERROR_INPUT_EXCEED, //终端输入超限
    ERROR_READING_INPUT, //读输入错误

    /* 重定向的错误信息 */
    ERROR_MANY_IN, //
    ERROR_MANY_OUT, //重定向文件过多
    ERROR_FILE_NOT_EXIST, //重定向文件不存在

    /* 管道的错误信息 */
    ERROR_PIPE,
    ERROR_PIPE_MISS_PARAMETER //管道缺失参数
};

//管道

//原子命令结构体声明
typedef struct
{
    /* data */
    char* command;//命令名
    char** argv;//命令参数
    char* redir_in_filename;//重定向输入文件名
    char* redir_out_filename;//重定向输出文件名
    int redir_type;//重定向类型
}atomic_command;

/// @brief 命令全局变量
char input[BUF_SIZE];//存储用户输入
char* background_commands[BUF_SIZE];//存储后台命令(并行命令 & )
char* atomCmdString[BUF_SIZE / 4];//用于存储原子命令
atomic_command* atomCmd[BUF_SIZE / 4];//原子命令结构体
size_t nread = 0;//从终端中读取的字符数量

//pipe fd
int* pipe_fd;

//分词符号
const char* SEP = " ";//命令分隔符
const char* PAR = "&";//后台并行
const char* PIPE = "|";//管道
const char* REDIR_IN = "<";//输入重定向
const char* REDIR_OUT = ">";//输出重定向
const char* REDIR_APP = ">>";//追加重定向


/// @brief 提示词
char username[BUF_SIZE];//用户名
char hostname[BUF_SIZE];//主机命
char curPath[BUF_SIZE];//当前工作目录
char prompt[BUF_SIZE];//提示词

/// @brief 初始化和更新提示词
void initGlobal();
void initPrompt();
void init_atomic_command(atomic_command* cmd);
int malloc_pipe_fd(int num);
void free_pipe_fd();
void updatePrompt();

/// @brief 获取用户输入函数集合
int getUserInput();//获取输入

/// @brief 命令分词函数集合
void splitUserInputIntoBGcmd();//将用户输入的命令通过&分词为后台命令
int splitAtomicCmd(char* cmdString, int index);//分词单个原子指令,检查重定向
void splitBGcmdAndRun(char* bgcommand);//解析每一个后台命令并运行, 后台命令包含管道和重定向

/// @brief 命令检查
void checkCommandvalidity();//检查命令的有效性，是否需要？
int checkRedirection(atomic_command* cmd);//检测原子命令是否有重定向

/// @brief 执行命令
int callCommand(int num_cmd);//调用命令

/// @brief 错误显示
void reportError(int ERROR_TYPE);

/// @brief DEBUG,打印原子指令结构体函数
void printAtomicCommand(atomic_command* cmd);

int main()
{
    int res = RESULT_NORMAL;

    //初始化提示词
    initPrompt();

    //循环获取用户输入
    while (1)
    {
        printf("%s", prompt);//输出提示词

        //获取用户输入,并且当用户输入过长时提示用户
        if ((res = getUserInput()) != RESULT_NORMAL)
        {
            reportError(res);
            continue;
        }

        //分割输出为多个后台命令
        splitUserInputIntoBGcmd();

        int idx = 0;
        while (background_commands[idx] != NULL)
        {
            //增量地运行后台命令
            splitBGcmdAndRun(background_commands[idx]);
            idx++;
        }
    }
    return 0;
}

void initGlobal()
{
    pipe_fd = NULL;

    for (int i = 0; i < BUF_SIZE; ++i)
        background_commands[i] = NULL;

    for (int i = 0; i < BUF_SIZE / 4; ++i)
    {
        atomCmdString[i] = NULL;
        atomCmd[i] = NULL;
    }
}

static void getUsername()
{
    struct passwd* pwd = getpwuid(getuid());
    strcpy(username, pwd->pw_name);
}

static void getHostname()
{
    gethostname(hostname, BUF_SIZE);
}

int getCurWorkDir() { // 获取当前的工作目录
    //获取目录失败时,返回NULL
    char* result = getcwd(curPath, BUF_SIZE);
    if (result == NULL)
        return ERROR_SYSTEM;
    return RESULT_NORMAL;
}

void initPrompt()
{
    /* 获取当前工作目录、用户名、主机名 */
    int result = getCurWorkDir();
    if (ERROR_SYSTEM == result) {
        fprintf(stderr, "\e[31;1mError: System error while getting current work directory.\n\e[0m");
        exit(ERROR_SYSTEM);
    }
    getUsername();
    getHostname();

    // 格式化提示符，存储在 prompt 中
    snprintf(prompt, BUF_SIZE, "\e[32;1m%s@%s:%s\e[0m$ ", username, hostname, curPath);
}

// 初始化函数
void init_atomic_command(atomic_command* cmd) {
    if (cmd == NULL) {
        return;
    }

    cmd->command = NULL;              // 初始化命令名为空
    cmd->argv = NULL;                 // 初始化参数列表为空
    cmd->redir_in_filename = NULL;    // 初始化输入重定向文件名为空
    cmd->redir_out_filename = NULL;   // 初始化输出重定向文件名为空
    cmd->redir_type = 0;              // 初始化重定向类型为 0
}

//分配管道空间
int malloc_pipe_fd(int num)
{
    if (num == 0)
        return RESULT_NORMAL;

    pipe_fd = (int*)malloc(num * sizeof(int));
    if (!pipe_fd)
        return ERROR_MEMORY_ALLOCATION;

    return RESULT_NORMAL;
}

//释放管道空间
void free_pipe_fd()
{
    if (pipe_fd)
        free(pipe_fd);
}

void updatePrompt()
{

}

//清空输入缓冲区
static void clear_stdin() {
    // 清除标准输入缓冲区中的剩余内容
    char ch;
    while ((ch = getchar()) != '\n' && ch != EOF); // 读取直到遇到换行符
}

/// @brief 
/// 使用fgets获取用户输入,三种情况
/// 1.输入字符数量 < BUF_SIZE - 1, 结尾加上'\n\0'
/// 2.输入字符数量 = BUF_SIZE - 1, 结尾只加'\0'
/// 3.输入字符数量 > BUF_SIZE, 截断[0,BUF_SIZE-1],在BUF_SIZE位置加'\0',剩余的输入在缓冲区,需要被清理
/// @return 获取输入的状态--1.正常 2.超出长度 3.获取失败
int getUserInput()
{
    nread = 0;//重置nread

    //获取用户输入
    if (fgets(input, sizeof(input), stdin) == NULL)
        return ERROR_READING_INPUT;

    //使用fgets会将\n包括在内
    nread = strlen(input);
    // 检查是否发生输入超出缓冲区大小
    // fgets发生截断时只读取BUF_SIZE-1个字符,并加上'\0',并且将超出的部分留存在缓冲区中
    if (nread == BUF_SIZE - 1 && input[BUF_SIZE - 2] != '\n') {
        printf("Input too long, please try again.\n");
        // 清除标准输入中的剩余内容
        clear_stdin();
        return ERROR_INPUT_EXCEED;
    }

    if (nread == BUF_SIZE - 1 && input[BUF_SIZE - 2] == '\n')
        input[BUF_SIZE - 2] = '\0';
    else
        input[nread - 1] = '\0';
    return RESULT_NORMAL;
}

//将用户输入的命令通过&分词为后台命令
void splitUserInputIntoBGcmd()
{
    char* token = NULL;
    int index = 0;

    //首先分割后台命令 & , 将后台命令存储在background_commands中
    token = strtok(input, PAR);
    while (token != NULL)
    {
        //打印
        //printf("%s\n", token);
        background_commands[index++] = token;
        token = strtok(NULL, PAR);
    }
    background_commands[index] = NULL;//最后一个有效命令的下一个为NULL用于终止判断
}

int splitAtomicCmd(char* cmdString, int index)
{
    char* in_token;
    int in_index = 0;

    // 检查是否已经为 atomCmd[index] 分配了内存，如果已经分配，则释放之前的内存
    if (atomCmd[index] != NULL) {
        // 如果已分配内存，先释放
        free(atomCmd[index]->argv);  // 释放 argv
        free(atomCmd[index]);         // 释放 atomic_command 结构体
    }
    atomCmd[index] = (atomic_command*)malloc(sizeof(atomic_command));
    init_atomic_command(atomCmd[index]);
    if (!atomCmd[index]) {
        perror("malloc failed for atomic_command");
        return ERROR_MEMORY_ALLOCATION; // 确保处理内存分配失败
    }
    atomCmd[index]->argv = (char**)malloc(PARAMETER_SIZE * sizeof(char*));
    if (!atomCmd[index]->argv) {
        perror("malloc failed for argv");
        free(atomCmd[index]);  // 释放之前为 atomCmd[index] 分配的内存
        return ERROR_MEMORY_ALLOCATION;
    }
    for (int i = 0; i < PARAMETER_SIZE; i++)
        atomCmd[index]->argv[i] = NULL;
    in_token = strtok(cmdString, SEP);//第一个划分出来的是命令
    atomCmd[index]->command = in_token;
    atomCmd[index]->argv[in_index++] = in_token;
    in_token = strtok(NULL, SEP);//继续分词
    while (in_token != NULL && in_index < PARAMETER_SIZE - 1) {
        atomCmd[index]->argv[in_index++] = in_token;
        //printf("%s ",in_token);
        in_token = strtok(NULL, SEP);
    }
    //printf("\n\n");

    //参数过多
    if (in_index == PARAMETER_SIZE - 1 && in_token != NULL)
        return ERROR_TOO_MANY_PARAMETER;
    atomCmd[index]->argv[in_index] = NULL;

    //检查是否有重定向
    int res = checkRedirection(atomCmd[index]);
    if (res != RESULT_NORMAL)
        return res;

    return RESULT_NORMAL;
}

//解析后台命令为原子命令,以及原子命令间的关系,并运行
void splitBGcmdAndRun(char* bgcommand)
{
    char* token = NULL;
    int index = 0;

    //使用 | 分割后台命令为原子命令
    token = strtok(bgcommand, PIPE);
    while (token != NULL)
    {
        atomCmdString[index++] = token;
        token = strtok(NULL, PIPE);
    }
    atomCmdString[index] = NULL;

    //将原子命令字符串转化成原子命令结构体
    for (int i = 0; i < index; ++i)
    {
        splitAtomicCmd(atomCmdString[i], i);
        //printAtomicCommand(atomCmd[i]);
        //printf("\n\n");
    }

    //执行每一条原子指令,重定向优先级高于管道
    callCommand(index);
}

void checkCommandvalidity()
{

}

//检查原子命令的重定向
int checkRedirection(atomic_command* cmd)
{
    if (cmd == NULL)
        return ERROR_SYSTEM;

    int index = 0;
    int redir_in_count = 0;
    int redir_out_count = 0;
    int redir_app_count = 0;

    // 遍历参数列表
    while (cmd->argv[index] != NULL)
    {
        // 检查输入重定向
        if (strcmp(cmd->argv[index], REDIR_IN) == 0) {
            // 只有输入重定向符号，没有文件名
            if (cmd->argv[index + 1] == NULL) {
                return ERROR_MISS_PARAMETER;  // 缺失输入文件名
            }

            if (redir_in_count > 0) {
                return ERROR_MANY_IN;  // 输入重定向符号出现多次
            }

            // 设置输入重定向文件名
            cmd->redir_in_filename = cmd->argv[index + 1];
            cmd->redir_type |= REDIR_INPUT;
            redir_in_count++;
            index += 2;  // 跳过输入重定向符号和文件名
        }
        // 检查输出重定向
        else if (strcmp(cmd->argv[index], REDIR_OUT) == 0) {
            // 只有输出重定向符号，没有文件名
            if (cmd->argv[index + 1] == NULL) {
                return ERROR_MISS_PARAMETER;  // 缺失输出文件名
            }

            if (redir_out_count > 0) {
                return ERROR_MANY_OUT;  // 输出重定向符号出现多次
            }

            // 设置输出重定向文件名
            cmd->redir_out_filename = cmd->argv[index + 1];
            cmd->redir_type |= REDIR_OUTPUT;
            redir_out_count++;
            index += 2;  // 跳过输出重定向符号和文件名
        }
        // 检查追加输出重定向
        else if (strcmp(cmd->argv[index], REDIR_APP) == 0) {
            // 只有追加重定向符号，没有文件名
            if (cmd->argv[index + 1] == NULL) {
                return ERROR_MISS_PARAMETER;  // 缺失追加文件名
            }

            if (redir_app_count > 0) {
                return ERROR_MANY_OUT;  // 追加重定向符号出现多次
            }

            // 设置追加输出重定向文件名
            cmd->redir_out_filename = cmd->argv[index + 1];
            cmd->redir_type |= REDIR_APPEND;
            redir_app_count++;
            index += 2;  // 跳过追加重定向符号和文件名
        }
        else {
            // 继续处理其他参数
            index++;
        }
    }

    // 检查输入文件是否存在
    if (cmd->redir_in_filename != NULL) {
        if (access(cmd->redir_in_filename, F_OK) == -1) {
            return ERROR_FILE_NOT_EXIST;  // 输入文件不存在
        }
    }

    // 检查输出文件是否存在
    if (cmd->redir_out_filename != NULL || cmd->redir_out_filename != NULL) {
        if (access(cmd->redir_out_filename, F_OK) == -1) {
            return ERROR_FILE_NOT_EXIST;  // 输出文件不存在
        }
    }

    return RESULT_NORMAL;  // 一切正常
}


int callCommand(int num_cmd)
{
    int i;
    int pipe_num = num_cmd - 1;
    malloc_pipe_fd(2 * pipe_num);

    // 创建管道
    for (i = 0; i < pipe_num; ++i)
    {
        if (pipe(pipe_fd + i * 2) == -1) {
            perror("pipe failed");
            exit(1);
        }
    }

    // 执行每个原子命令
    for (i = 0; i < num_cmd; ++i)
    {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork failed");
            exit(1);
        }

        if (pid == 0) {  // 子进程
            // 处理输入重定向
            if (atomCmd[i]->redir_in_filename != NULL) {
                int in_fd = open(atomCmd[i]->redir_in_filename, O_RDONLY);
                if (in_fd == -1) {
                    perror("Input redirection failed");
                    exit(1);
                }
                if (dup2(in_fd, STDIN_FILENO) == -1) {
                    perror("dup2 stdin failed");
                    exit(1);
                }
                close(in_fd);
            }

            // 处理输出重定向
            if (atomCmd[i]->redir_out_filename != NULL) {
                int out_fd;
                if (atomCmd[i]->redir_type == 1) {  // 处理 `>`
                    out_fd = open(atomCmd[i]->redir_out_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                }
                else if (atomCmd[i]->redir_type == 2) {  // 处理 `>>`
                    out_fd = open(atomCmd[i]->redir_out_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
                }
                if (out_fd == -1) {
                    perror("Output redirection failed");
                    exit(1);
                }
                if (dup2(out_fd, STDOUT_FILENO) == -1) {
                    perror("dup2 stdout failed");
                    exit(1);
                }
                close(out_fd);
            }

            // 如果不是第一个命令，重定向输入为前一个管道的读端
            if (i > 0) {
                if (dup2(pipe_fd[(i - 1) * 2], STDIN_FILENO) == -1) {
                    perror("dup2 stdin failed");
                    exit(1);
                }
            }

            // 如果不是最后一个命令，重定向输出为当前管道的写端
            if (i < num_cmd - 1) {
                if (dup2(pipe_fd[i * 2 + 1], STDOUT_FILENO) == -1) {
                    perror("dup2 stdout failed");
                    exit(1);
                }
            }

            // 关闭所有管道文件描述符
            for (int j = 0; j < 2 * (num_cmd - 1); ++j) {
                close(pipe_fd[j]);
            }

            // 执行命令
            //printAtomicCommand(atomCmd[i]);
            execvp(atomCmd[i]->command, atomCmd[i]->argv);
            perror("execvp failed");
            exit(1);  // 如果 execvp 失败，退出子进程
        }
    }

    // 父进程关闭所有管道文件描述符
    for (int i = 0; i < 2 * (num_cmd - 1); ++i) {
        close(pipe_fd[i]);
    }

    // 父进程等待所有子进程
    for (int i = 0; i < num_cmd; ++i) {
        wait(NULL);
    }
}

void reportError(int ERROR_TYPE)
{

}

//DEBUG
//打印atomic_command结构体内容
void printAtomicCommand(atomic_command* cmd)
{
    if (cmd == NULL) {
        printf("Error: atomic_command is NULL\n");
        return;
    }

    // 打印命令名，确保处理NULL情况
    if (cmd->command == NULL) {
        printf("Command: NULL\n");
    }
    else {
        printf("Command: %s\n", cmd->command);
    }

    // 打印命令参数
    printf("Arguments: ");
    if (cmd->argv != NULL) {
        int i = 0;
        while (cmd->argv[i] != NULL) {
            printf("'%s' ", cmd->argv[i]);
            i++;
        }
    }
    printf("\n");

    // 打印输入重定向文件名
    if (cmd->redir_in_filename != NULL) {
        printf("Input redirection: %s\n", cmd->redir_in_filename);
    }

    // 打印输出重定向文件名
    if (cmd->redir_out_filename != NULL) {
        printf("Output redirection: %s\n", cmd->redir_out_filename);
    }

    // 打印重定向类型
    printf("Redirection type: ");
    if (cmd->redir_type & REDIR_INPUT) {
        printf("Input ");
    }
    if (cmd->redir_type & REDIR_OUTPUT) {
        printf("Output ");
    }
    if (cmd->redir_type & REDIR_APPEND) {
        printf("Append ");
    }
    if (cmd->redir_type == REDIR_NONE) {
        printf("None ");
    }
    printf("\n");
}