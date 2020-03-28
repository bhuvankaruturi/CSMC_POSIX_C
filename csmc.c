#include "stdio.h"
#include "stdlib.h"
#include "assert.h"
#include "pthread.h"
#include "semaphore.h"
#include "errno.h"

int debug = 0; //mode of program execution
int empty_chairs = 0;

// inputs from the user
int studs = 0;
int tuts = 0;
int chairs = 0;
int help = 0;

// variable to keep track of tutoring progress
int tutored_sess = 0;
int total_reqs = 0;
int studs_tutored = 0;

struct student **priority_queue;
int *tutor_id;

// semaphors
sem_t coordinator;
sem_t student;
sem_t tutor;
sem_t *tutor_session;

// mutex locks
pthread_mutex_t studs_lock;
pthread_mutex_t tuts_lock;

pthread_mutex_t priority_queue_lock;
pthread_mutex_t tutors_queue_lock;
pthread_mutex_t tutor_id_lock;

pthread_mutex_t empty_chairs_lock;
pthread_mutex_t tutored_sess_lock;
pthread_mutex_t total_reqs_lock;
pthread_mutex_t studs_tutored_lock;

// student struct, node in a doubly linked list
struct student
{
  int stud_id;
  struct student *next;
};

//tutors queue head
struct student *tut_head = NULL;

// tutors routine
static void *
tutors_routine(void *arg)
{
  long tut_id = (long)arg;
  while (1)
  {
    // wait for the coordinator to signal
    sem_wait(&coordinator);
    pthread_mutex_lock(&tutors_queue_lock);
    // if no students are present in the tutor queue
    // terminate the tutor thread
    if (tut_head == NULL)
    {
      pthread_mutex_unlock(&tutors_queue_lock);
      pthread_mutex_lock(&tuts_lock);
      tuts--;
      if (tuts == 0)
        sem_post(&tutor);
      pthread_mutex_unlock(&tuts_lock);
      break;
    }
    // select the first student in the list to tutor
    struct student *next_stud = tut_head;
    tut_head = tut_head->next;
    pthread_mutex_unlock(&tutors_queue_lock);
    int stud_id = next_stud->stud_id;
    // array to communicate with students about the tutor assigned to them
    pthread_mutex_lock(&tutor_id_lock);
    tutor_id[stud_id] = tut_id;
    pthread_mutex_unlock(&tutor_id_lock);
    // increment the count of currently tutored students by 1
    pthread_mutex_lock(&studs_tutored_lock);
    studs_tutored++;
    pthread_mutex_unlock(&studs_tutored_lock);
    // signal the selected student waiting for tutor to recieve tutoring
    sem_post(&tutor_session[stud_id]);
    usleep(200); // mimic tutoring by sleeping for 0.2 ms
    // increment total tutoring sessions by 1
    pthread_mutex_lock(&studs_tutored_lock);
    pthread_mutex_lock(&tutored_sess_lock);
    tutored_sess++;
    printf("Tu: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d.\n", stud_id + 1, tut_id + 1, studs_tutored, tutored_sess);
    studs_tutored--;
    pthread_mutex_unlock(&tutored_sess_lock);
    pthread_mutex_unlock(&studs_tutored_lock);
  }
  if (debug)
    printf("Tu %d : am done with tutoring\n", tut_id + 1);
}

// student threads routine
static void *
student_routine(void *arg)
{
  long stud_id = (long)arg;
  int self_help = help;
  int my_tutor_id;
  if (debug)
    printf("St %d: I am a new student\n", stud_id + 1);
  while (self_help > 0)
  {
    pthread_mutex_lock(&empty_chairs_lock);
    // if no chairs are available, go to sleep for 2ms
    if (empty_chairs == 0)
    {
      pthread_mutex_unlock(&empty_chairs_lock);
      printf("St: Student %d found no empty chair. Will try again later\n", stud_id + 1);
      usleep(2000); // programming
      continue;
    }
    else
    {
      // if a chair is available, add the thread to
      // the head of the linked list at its priority level
      pthread_mutex_lock(&priority_queue_lock);
      if (priority_queue[self_help - 1] == NULL)
      {
        struct student *new_node = (struct student *)malloc(sizeof(struct student));
        new_node->stud_id = stud_id;
        new_node->next = NULL;
        priority_queue[self_help - 1] = new_node;
      }
      else
      {
        struct student *temp = priority_queue[self_help - 1];
        while (temp->next != NULL)
          temp = temp->next;
        struct student *new_node = (struct student *)malloc(sizeof(struct student));
        new_node->stud_id = stud_id;
        new_node->next = NULL;
        temp->next = new_node;
      }
      pthread_mutex_unlock(&priority_queue_lock);
      empty_chairs--; // decrement the number of empty chairs
      printf("St: Student %d takes a seat. Empty chairs = %d\n", stud_id + 1, empty_chairs);
    }
    pthread_mutex_unlock(&empty_chairs_lock);
    // wake up coordinator and wait for tutor
    sem_post(&student);
    sem_wait(&tutor_session[stud_id]);
    // getting help
    usleep(200); // mimic recieving tutoring by sleeping for 0.2ms
    // tutor id
    pthread_mutex_lock(&tutor_id_lock);
    my_tutor_id = tutor_id[stud_id];
    tutor_id[stud_id] = -1;
    pthread_mutex_unlock(&tutor_id_lock);
    printf("St: Student %d received help from Tutor %d\n", stud_id + 1, my_tutor_id + 1);
    // decrement the remaining help required by 1
    self_help--;
  }
  pthread_mutex_lock(&studs_lock);
  studs = studs - 1;
  // if the current student is the last student left then signal the coordinator
  if (studs == 0)
  {
    pthread_mutex_unlock(&studs_lock);
    sem_post(&student);
    pthread_exit(NULL);
  }
  pthread_mutex_unlock(&studs_lock);
  pthread_exit(NULL);
}

//coordinators routine
static void *
coordinator_routine()
{
  int i, priority;
  while (1)
  {
    // wait for a student to signal
    sem_wait(&student);
    // all the student threads have terminated.
    pthread_mutex_lock(&studs_lock);
    if (studs == 0)
    {
      pthread_mutex_unlock(&studs_lock);
      break;
    }
    pthread_mutex_unlock(&studs_lock);
    pthread_mutex_lock(&priority_queue_lock);
    struct student *selected_chair;
    // get the student with highest priority
    for (i = help - 1; i >= 0; i--)
    {
      selected_chair = priority_queue[i];
      if (selected_chair != NULL)
      {
        priority_queue[i] = priority_queue[i]->next;
        priority = i + 1;
        break;
      }
    }
    // if no student in the queue, continue
    if (selected_chair == NULL)
    {
      pthread_mutex_unlock(&priority_queue_lock);
      continue;
    }
    // total request
    pthread_mutex_lock(&total_reqs_lock);
    total_reqs++;
    pthread_mutex_unlock(&total_reqs_lock);
    // get the next stud_id
    int stud_id = selected_chair->stud_id;
    free(selected_chair);
    pthread_mutex_unlock(&priority_queue_lock);

    // add the selected student to the tutors queue
    struct student *next_node = (struct student *)malloc(sizeof(struct student));
    pthread_mutex_lock(&tutors_queue_lock);
    next_node->stud_id = stud_id;
    next_node->next = NULL;
    if (tut_head == NULL)
      tut_head = next_node;
    else
    {
      struct student *temp = tut_head;
      while (temp->next != NULL)
        temp = temp->next;
      temp->next = next_node;
    }
    pthread_mutex_unlock(&tutors_queue_lock);
    
    // print coordinators output
    pthread_mutex_lock(&total_reqs_lock);
    pthread_mutex_lock(&empty_chairs_lock);
    printf("Co: Student %d with priority %d in queue. Waiting students now = %d. Total requests = %d\n", stud_id + 1, priority, chairs - empty_chairs, total_reqs);
    pthread_mutex_unlock(&total_reqs_lock);
    // increment the number of available chairs
    empty_chairs++;
    pthread_mutex_unlock(&empty_chairs_lock);
    // signal tutors
    sem_post(&coordinator);
  }

  // signal all the tutors to terminate
  pthread_mutex_lock(&tuts_lock);
  for (i = 0; i < tuts; i++)
    sem_post(&coordinator);
  pthread_mutex_unlock(&tuts_lock);

  // wait for the last tutor to post
  sem_wait(&tutor);

  if (debug)
    printf("Co : done with execution\n");
}

int main(int argc, char *argv[])
{
  long i;
  // check for valid number of inputs
  if (argc != 5)
  {
    fprintf(stderr, "Invalid number of arguments to %s\n", argv[0]);
    return 1;
  }

  studs = atoi(argv[1]);
  tuts = atoi(argv[2]);
  chairs = atoi(argv[3]);
  help = atoi(argv[4]);

  // check if the inputs are all positive or else write to stderr and exit
  if (studs <= 0 || tuts <= 0 || chairs <= 0 || help < 0)
  {
    fprintf(stderr, "Invalid values entered\n");
    return 1;
  }

  // if help is zero, need not run any tutoring sessions
  if (help == 0)
  {
    return 0;
  }

  if (debug)
  {
    printf("Students: %d\n", studs);
    printf("Tutors: %d\n", tuts);
    printf("chairs: %d\n", chairs);
    printf("Help: %d\n", help);
  }

  //initialization
  int l_tuts = tuts;
  int l_studs = studs;
  empty_chairs = chairs;
  tut_head = NULL;
  priority_queue = malloc(help * sizeof(struct student *));
  tutor_id = (int *)malloc(studs * sizeof(int));
  tutor_session = (sem_t *)malloc(studs * sizeof(sem_t));

  // set initial priority queues
  for (i = 0; i < help; i++)
  {
    priority_queue[i] = NULL;
  }
  // set tutor assigned to each student to -1
  for (i = 0; i < studs; i++)
  {
    tutor_id[i] = -1;
  }

  //thread declaration
  pthread_t *stud_threads;
  pthread_t *tut_threads;
  pthread_t coordinator_thread;

  stud_threads = malloc(sizeof(pthread_t) * studs);
  tut_threads = malloc(sizeof(pthread_t) * tuts);

  //semaphor initialization
  sem_init(&coordinator, 0, 0);
  sem_init(&student, 0, 0);
  sem_init(&tutor, 0, 0);
  for (i = 0; i < studs; i++)
  {
    sem_init(&tutor_session[i], 0, 0);
  }

  // thread creation
  assert(pthread_create(&coordinator_thread, NULL, coordinator_routine, NULL) == 0);
  pthread_setname_np(&coordinator_thread, "coord");

  for (i = 0; i < l_studs; i++)
  {
    int err = pthread_create(&stud_threads[i], NULL, student_routine, (void *)i);
    if (err != 0)
    {
      errno = err;
      perror("error in pthread_create for student thread");
      fprintf(stderr, "unable to create student thread %d\n", i + 1);
    }
    assert(err == 0);
    char *name = (char *)i;
    pthread_setname_np(&stud_threads[i], "stud");
  }

  for (i = 0; i < l_tuts; i++)
  {
    int err = pthread_create(&tut_threads[i], NULL, tutors_routine, (void *)i);
    if (err != 0)
    {
      errno = err;
      perror("error in pthread_create for a tutor thread");
      fprintf(stderr, "unable to create tutor thread %d\n", i + 1);
    }
    assert(err == 0);
    pthread_setname_np(&tut_threads[i], "tut");
  }

  // wait for threads to join
  for (i = 0; i < l_studs; i++)
  {
    pthread_join(stud_threads[i], NULL);
  }
  for (i = 0; i < l_tuts; i++)
  {
    pthread_join(tut_threads[i], NULL);
  }
  pthread_join(coordinator_thread, NULL);

  if (debug)
    printf("program execution completed!\n");
}
