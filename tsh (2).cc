// 
// tsh - A tiny shell program with job control
// 
// Devin Burke -- debu7497
//Thomas Payne

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{

  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
	//printf("beginning of eval \n");
	char *argv[MAXARGS];
	pid_t pid; //data type of process ID
	sigset_t badChild; //will be used to suppress child signals 	
  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
	

	int bg = parseline(cmdline, argv);
	if (argv[0] == NULL) 
	{
		return;   /* ignore empty lines */
	} 
	
	if(!builtin_cmd(argv)) //if not built in command then we need child
	{
		

		sigemptyset(&badChild);
		sigaddset(&badChild, SIGCHLD); //SIGCHILD
		sigprocmask(SIG_BLOCK, &badChild, 0);//block child
		
		//printf("before fork in eval \n");
		
		if((pid=fork()) == 0) // spawns child
		{
			//printf("inside fork \n");
				
			sigprocmask(SIG_UNBLOCK,&badChild,0);//unblock
			setpgid(0,0); //groups children under same ID
			//now we can execute command using child
			if(execve(argv[0], argv, environ) < 0)
			{
				
				//if we get negative then error, print and quit
				printf("%s: Command not found\n", argv[0]);
				exit(0);
			}
		}
		if(!bg) // this is now our parent, need to wait for fg^ to terminate
		{
			//add job, unblock child signal, wait till termination,reap
		
			//printf("inside fg eval \n");
			addjob(jobs,pid,FG,cmdline);
			//printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline); 
			sigprocmask(SIG_UNBLOCK,&badChild,0);
			waitfg(pid);
			
		}
		else //background
		{
			//printf("inside bg eval \n");
			
			addjob(jobs,pid,BG,cmdline);
		    printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline); 
			sigprocmask(SIG_UNBLOCK,&badChild,0);
			//print because its background and cant see
			
		}
	}
	return;
}
		
	

/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
	
	//printf("beginning of builtin_cmd \n");
	
	string cmd(argv[0]);
	if(cmd == "quit")
	{

		//printf("quitting now\n");
		exit(0);
	}
	if(cmd == "jobs") // print out jobs list
	{
		listjobs(jobs);
		return 1;
	}
	if(cmd == "bg" || cmd == "fg")
	{
		//printf("calling do_bgfg \n");
		do_bgfg(argv);
		return 1;
	}

	
	return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
	//printf("beginning of do_bgfg \n");
	//printf("error 3 \n");
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n",argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
	string cmd(argv[0]);
	if(cmd == "fg")
	{
		//printf("inside do_bgfg FG \n");
		
		if(jobp->state == ST)//then job has stopped, so we force it to conintue
		{
			kill(-jobp->pid,SIGCONT);//we want to send SIGCONT to every process so we use negative
		}
			jobp->state =FG; // foreground job
			waitfg(jobp->pid); //ensure finish so we wait
			
	}
	else if(cmd == "bg")
	{
		//printf("inside do_bgfg BG \n");
		//continue regardless because its background
		kill(-jobp->pid,SIGCONT);
		jobp->state=BG; // background
		printf("[%d] (%d) %s",jobp -> jid, jobp -> pid, jobp->cmdline); //print because in background, cant see it
		
	}
	else
	{
		unix_error("error within do_bgfg");
	}
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
	//printf("inside waitfg \n");
	while(pid==fgpid(jobs))
	{
		sleep(0); //continue to loop until pid is no longer listed under fg jobs list
	}
	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
	pid_t pid;
	int status = -1;
	//printf("beginning of sigchld_handler \n");
	while((pid = waitpid(-1, &status, WUNTRACED|WNOHANG))>0){
	// this loop contains the set of the child process, waitpid returns the PID of a terminated child and the loop will continue to iterate until there are no more children to delete hence there are no more signals to investigate
		if(WIFEXITED(status)) //if child terminated normally
		{
			//printf("Job [%d] (%d) deleted normally  \n",pid2jid(pid),pid);
			deletejob(jobs,pid);
		}
		if(WIFSIGNALED(status)) //if child terminated by signal not caught
		{
			printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,WTERMSIG(status));//prints out signal that caused process to terminate
			deletejob(jobs, pid);
		}
		if(WIFSTOPPED(status)) //if child that caused the return is currently stopped
		{
			getjobpid(jobs,pid)->state=ST; //changes state to stopped, do_bgfg should continue?
			printf("Job [%d] (%d) stopped by signal %d \n",pid2jid(pid),pid,WSTOPSIG(status)); //prints out signal that caused process to stop

		}
	}
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
	//printf("beginning of sigint_handler \n");
	//printf("inside sig int handler \n");
	pid_t sigJob = fgpid(jobs);//this gets us the fg jobs so that we can place our sigJob signaller in its list
	
	if(sigJob!=0)  // means we have current fg tasks to reap
	{
		kill(-sigJob, SIGINT); // so we send SIGINT to every process in our group PID
	}
		
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{

	//printf("inside sigstp_handler");
	pid_t sigJob = fgpid(jobs); // this gets us the fg jobs so that we can place our sigJob signaller in the list
	
	if(sigJob!=0)//means we have current fg tasks to pause also
	{
		kill(-sigJob, SIGTSTP); //so we send SIGSTP to every process in our group PID
	}	
  return;
}

/*********************
 * End signal handlers
 *********************/




