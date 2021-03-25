
#include <stdio.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define CMP strcmp
#define LEN strlen
#include <cd.h>
#include <echo.h>
#include <shellutil.h>

#define MAX_LENGTH 1024
#define BGD 1
#define STOPPED 2
#define proc_range 32768

typedef struct procinfo{ //Struct for maintaining processes created by user
	pid_t pid;
	int type;
}procinfo;

char cache[proc_range+10][20];//to store the names of processes
char commandtot[MAX_LENGTH];
pid_t pid,pid1,shellpid;

procinfo stack[proc_range+10];//Stack to mainitain processes
int top=-1; // stack head 


char *usrname,*token,*hostname,*a[102],*mypath,*origin;
char delim[4]=" \t\n";
char cmd[MAX_LENGTH],cmd_given[MAX_LENGTH],ch,homedir[MAX_LENGTH],curdir[MAX_LENGTH];

char temp[MAX_LENGTH],tmp1[MAX_LENGTH]; //Temporary variables to do process 
unsigned char buffer[4096];


void stop_handler(int signo){
	if( signo == SIGTSTP )	
		if(pid == 0)
			kill(getpid(),SIGSTOP);
}

/* Handles the SIGINT signal */

void sigint_handler(int signo){
	if( signo == SIGINT )	
		if(getpid() != shellpid)
			kill(getpid(),SIGKILL);
}

/* Executes the given command */

void my_execute(int in,int out,int *sig,int *bgflag,char* shellname,int *pipefl){
	int l,instate=0,outstate=0;
	char curstdin[MAX_LENGTH],curstdout[MAX_LENGTH];
	for(l=0 ; cmd[l]!='\n' && l<LEN(cmd) ; l++){
		if(cmd[l]=='>'){
			if (cmd[l+1]=='>'){
				outstate=2;
				cmd[l]=' ';
				l++;
			}
			else outstate = 1;
			cmd[l]=' ';
			l++;
			while(cmd[l]==' ')l++;
			int f=0;
			for(;cmd[l]!=' '&&cmd[l]!='<'&&cmd[l]!='\n';l++){
				curstdout[f++]=cmd[l];
				cmd[l]=' ';
			}
			curstdout[f]='\0';
		}
		if(cmd[l]=='<'){
			instate = 1;
			cmd[l]=' ';
			l++;
			while(cmd[l]==' ')l++;
			int f=0;
			for(;cmd[l]!=' '&&cmd[l]!='>'&&cmd[l]!='\n';l++){
				curstdin[f++]=cmd[l];
				cmd[l]=' ';
			}
			if(cmd[l]=='>') l--;
			curstdin[f]='\0';
		}
	}

	if(curstdin[LEN(curstdin)-1]=='\n')
		curstdin[LEN(curstdin)-1]='\0';
	if(curstdout[LEN(curstdout)-1]=='\n')
		curstdout[LEN(curstdout)-1]='\0';
	if(instate==1){
		in = open(curstdin, O_RDONLY);
		dup2(in,0);
	}
	if(outstate==1){
		out = open(curstdout,O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		dup2(out,1);
	}
	else if (outstate==2){
		out = open(curstdout,O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		dup2(out,1);
	}
	int cmd_len = LEN(cmd);      
	token = (char *)strtok(cmd,delim);
	if(token[LEN(token)-1] == '\n')
		token[LEN(token)-1] = '\0';
	if(CMP(token,"echo")==0)            
		implement_echo(cmd+LEN(token)+1);
	else if(CMP(token,"pwd")==0)         
		printf("%s\n",curdir);
	else if(CMP(token,"cd")==0){         
		cmd[cmd_len-1]='\0';
		token = strtok(NULL,delim);
		if(token==NULL)chdir(homedir);
		else implement_cd(token,homedir,shellname,cmd,curdir);
	}
	else if(CMP(token,"quit")==0||CMP(token,"exit")==0)        
		_exit(0);
	else if(CMP(token,"fg") == 0){
		pid = fork();
		if(pid!=0){
			pid_t p = stack[top].pid;
			token = strtok(NULL,"\n ");
			int num = 0,k;
			if(token!=NULL){
				for(k=0;token[k]!='\0';k++){
					num = num*10 + token[k]-'0';
				}
				num--;
				p=stack[num].pid;
			}
			if(num > top){
				printf("Sorry no such job\n");
				return;
			}
			for(k=num;k<top;k++)
				stack[k]=stack[k+1];
			kill(p,SIGCONT);
			top--;
			while(1){
				pid_t pid_check = waitpid(p,sig,WNOHANG|WUNTRACED);
				if(pid_check == p){
					if(WIFSTOPPED(*sig)){
						top++;
						stack[top].pid = pid_check;
						stack[top].type = STOPPED;
						break;
					}
					else if(WIFEXITED(*sig) || WIFSIGNALED(*sig))
						break;
				}
			}
		}
		else
			_exit(0);
	}
	else if(CMP(token,"bg")==0){
		kill(stack[top].pid,SIGCONT);
		stack[top].type = BGD;
	}
	else if(CMP(token,"jobs")==0){
		int k,index=0;
		for(k=0;k<=top;k++){
			int err=kill(stack[k].pid,0);
			if(err==-1 && errno==ESRCH){
				int ti;
				for(ti=k;ti<top;ti++)stack[ti]=stack[ti+1];
				k--;
				top--;
			}
		}
		for(k=0;k<=top;k++){
			printf("[%d]\t",k+1 );
			if(stack[k].type==BGD)
				printf("Running\t");
			else
				printf("Stopped\t");
			print_command(stack[k].pid,tmp1,buffer);
		}
	}
	else if(CMP(token,"kjob")==0){
		token = strtok(NULL," ");
		int jobno=0,k,sign=0;
		pid_t p;
		if(token!=NULL){
			for(k=0;token[k]!='\0';k++){
				jobno = jobno*10 + token[k]-'0';
			}
			jobno--;
			if(jobno > top){
				printf("Sorry no such job exists\n");
				return;
			}
			p=stack[jobno].pid;
		}
		else{
			printf("Few arguments provided\n");
			return;
		}
		token = strtok(NULL," ");
		if(token!=NULL){
			for(k=0;token[k]!='\0';k++){
				sign = sign*10 + token[k]-'0';
			}
		}
		else{
			printf("Few arguments provided\n");
			return;
		}
		kill(p,sign);

	}
	else if(CMP(token,"overkill")==0){
		int k;
		for(k=0;k<=top;k++)
			kill(stack[k].pid,9);
		top=-1;
	}
	else{					
		int i=LEN(cmd)+1;
		while(cmd[i]!='\0')if (cmd[i++]=='&') *bgflag=1;
		if(*bgflag==1 && *pipefl==1){
			printf("%s: ",shellname);
			puts("syntax error near unexpected token `|\'");
			return;
		}
		//Handle functions other than cd,echo,pwd and exit	
		pid = fork();//Creating a new process
		if(pid==0){			
			//Child process
			int i=0;
			while(token!=NULL){	//Tokenizing the given command for giving it to execvp
				a[i]=(char*)malloc(LEN(token)*sizeof(char));
				if(CMP(token,"&")==0);//avoiding & into the command
/* Executes the given command */
				else strcpy(a[i++],token);
				token = strtok(NULL," \n");
			}
			int j=0;
			if (CMP(a[0],"ls")==0||CMP(a[0],"grep")==0){
				a[i]=(char *)malloc(20*sizeof(char));
				strcpy(a[i++],"--color=auto");
			}

			a[i]=NULL;//indicating end of command
			int err = execvp(a[0],a);		//Execute the command
			if (err==-1 && errno==2 )fprintf(stderr,"%s: command not found\n",a[0]);
			int k;

			for (k=0;k<i;k++)free(a[k]);
			_exit(0);
		}

		else{
			if(*bgflag!=0){
				printf("[%d]\n",pid);
			}
			cache_store(temp,pid,cache);
			if(*bgflag==0){
				while(1){
					pid_t pid_check = waitpid(pid,sig,WNOHANG|WUNTRACED);
					if(pid_check == pid){
						if(WIFSTOPPED(*sig)){
							top++;
							stack[top].pid = pid;
							stack[top].type = STOPPED;
							break;
						}
						else if(WIFEXITED(*sig))
							break;
/* Executes the given command */
						else if(WIFSIGNALED(*sig))
							break;
					}
				}
			}
			else{
				top++;
				stack[top].pid=pid;
				stack[top].type=BGD;
			}
		}
	}
	return ;
}

/* Main code */

int main(int argc,char *argv[],char *envp[]){

	shellpid=getpid();

	origin=(char *)malloc(MAX_LENGTH*sizeof(char)); 
	mypath=(char *)malloc(MAX_LENGTH*sizeof(char));
	usrname=(char*)malloc(MAX_LENGTH*sizeof(char));//To store the user name 
	token=(char*)malloc(MAX_LENGTH*sizeof(char)); //To store the tokens
	hostname=(char *)malloc(MAX_LENGTH*sizeof(char));//To store the hostname

	int sig;

	signal(SIGTSTP,stop_handler);
	signal(SIGINT,sigint_handler);

	getcwd(homedir,MAX_LENGTH);
	usrname=getenv("USER");
	gethostname(hostname,MAX_LENGTH);


	while(1){
		int bgflag = 0,i;

		bzero(cmd_given,MAX_LENGTH);

		while((pid=waitpid(-1,&sig,WNOHANG|WUNTRACED))>0) {
			int state = 0,k,l;
			for(k=0;k<=top;k++){

				if(stack[k].pid==pid){
					state=1;
					break;
				}
			}
			if(WIFEXITED(sig) && state == 1){
				printf("Process %s with [%d] exited\n",cache[pid],pid);
				for(l=k;l<top;l++)
					stack[l]=stack[l+1];
				top--;
			} 
		}

		print_prompt(usrname,hostname,homedir,curdir);  

		if(!fgets(cmd_given,MAX_LENGTH,stdin)){
			puts("exit");
			_exit(0);
		}
		if(CMP(cmd_given,"\n")==0)continue;
		int index=0;
		int fd[2],in=0,out;
		int stdintemp=dup(0);
		int stdouttemp=dup(1);
		int pipefl=0;
		while(cmd_given[index]!='\0'){ 
			int j=0;
			bzero(cmd,MAX_LENGTH);
			getcwd(curdir,MAX_LENGTH);
			while(cmd_given[index]!='\0' && cmd_given[index]!=';'&&cmd_given[index]!='\n'&&cmd_given[index]!='|'){
				cmd[j]=cmd_given[index];                              
				j++;
				index++;
			}
			int fl=1,l;
			for(l=0;l<j;l++){
				if(cmd[l]!='\t' && cmd[l]!=' ' && cmd[l]!='\n'){
					fl=0;
					break;
				}
			}
			if((cmd_given[index]==';'||cmd_given[index]=='|') && fl==1){
				printf("%s: ",argv[0]);
				printf("syntax error near unexpected token ");
				printf("\'");
				printf("%c",cmd_given[index]);
				printf("\'\n");
				break;
			}
			else if (CMP(cmd,"\n")==0)continue;
			else if (fl==1 || j==0){
				if(pipefl==1)
					puts("Wrong syntax");
				break;
			}
			char c = cmd_given[index];
			if(c=='|'){
				pipefl=1;
				pipe(fd);
				if((pid1=fork())==0){

					if(in!=0){
						//not stdin
						dup2(in,0);
						close(in);
					}

					if(fd[1]!=1){
						//not stdout 
						dup2(fd[1],1);
						close(fd[1]);
					}
					my_execute(in,out,&sig,&bgflag,argv[0],&pipefl);
					_exit(0);
				}
				else{
					index++;
					wait(NULL);
					close(fd[1]);
					in = fd[0];
				}

			}
			else{
				pipefl=0;
				if(in!=0){
					dup2(in,0);
					close(in);
				}
				index++;
				cmd[j++]='\n';			
				cmd[j++]='\0';
				my_execute(in,out,&sig,&bgflag,argv[0],&pipefl);
				in=0;
				dup2(stdintemp,0);
				dup2(stdouttemp,1);
				close(stdintemp);
				close(stdouttemp);
			}
			if (top<-1)top=-1;
		}
	}
	return 0;
}
