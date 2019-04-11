#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* Add struct. */
struct fd_file_map {
    int fd;
    struct file * file;
    struct list_elem elem;
};

/* Add global variable. */
struct lock filesys_lock;


void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

void
syscall_exit (int status) {
  printf("%s: exit(%d)\n", &thread_current ()->name, status);
  thread_exit();
}

void validate_addr (void *ptr) {
  if (!is_user_vaddr(ptr)) {
    syscall_exit(-1);
    //thread_exit(); or process_exit();
  }
  //check size
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  validate_addr((void*) args[0]);
  validate_addr((void*) args[1]);
  validate_addr((void*) args[2]);
  validate_addr((void*) args[3]);

  //printf("System call number: %d\n", args[0]);
  if (args[0] == SYS_EXIT) {
    f->eax = process_exit();
    (thread_current ()->wait_status)->exit_code = args[1];
    printf("%s: exit(%d)\n", &thread_current ()->name, args[1]);
    thread_exit();
    //syscall_exit(args[1]);
  } else if (args[0] == SYS_HALT) {
    //printf("System power off\n");
    shutdown_power_off();
  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
    //printf("Practice: %d + 1 = %d\n", args[1], args[1] + 1);
  } else if (args[0] == SYS_EXEC) {
    f->eax = process_execute(args[1]);
    //printf("Execute: %d\n", args[1]);
  } else if (args[0] == SYS_WAIT) {
    f->eax = process_wait(args[1]);
    //printf("Wait status: %d\n", args[1]);
  } else if (args[0] == SYS_CREATE) {
    char *file = args[1];
    unsigned initial_size = args[2];
    f->eax = create(file, initial_size);
  } else if (args[0] == SYS_REMOVE) {
    char *file = args[1];
    f->eax = remove(file);
  } else if (args[0] == SYS_OPEN) {
    char *file = args[1];
    f->eax = open(file);
  } else if (args[0] == SYS_FILESIZE) {
    int fd = args[1];
    f->eax = filesize(fd);
  } else if (args[0] == SYS_READ) {
    int fd = args[1];
    void * buff = args[2];
    size_t size = args[3];
    f->eax = read(fd, buff, size);
  } else if (args[0] == SYS_WRITE) {
    int fd = args[1];
    void * buff = args[2];
    size_t size = args[3];
    f->eax = write(fd, buff, size);
  } else if (args[0] == SYS_SEEK) {
    int fd = args[1];
    unsigned position = args[2];
    seek(fd, position);
  } else if (args[0] == SYS_TELL) {
    int fd = args[1];
    f->eax = tell(fd);
  } else if (args[0] == SYS_CLOSE) {
    int fd = args[1];
    close(fd);
  }

}

/* Some helper functions. */

int add_file(struct file* file)
{
  struct thread *cur_thread = thread_current();
  int assigned = -1;
  if (cur_thread->next_fd >= 40960) {
    cur_thread->next_fd = 3;
    assigned = 2;
  }
  else {
    assigned = cur_thread->next_fd;
    cur_thread->next_fd++;
  }
  struct fd_file_map* new_fd_map = malloc(sizeof(struct fd_file_map));
  if (new_fd_map == NULL) {
    return -1;
  }
  new_fd_map->fd = assigned;
  new_fd_map->file = file;
  list_push_back(&cur_thread->fd_list, &new_fd_map->elem);
  return assigned;
}

/* remove the mapping struct according to fd in current thread's fd_list. */
void remove_file(int fd)
{
  struct thread *cur_thread = thread_current();
  struct list_elem *e;
  struct fd_file_map *cur_map;

  for (e = list_begin (&cur_thread->fd_list); e != list_end (&cur_thread->fd_list);
       e = list_next (e))
    {
      struct fd_file_map* cur_map = list_entry (e, struct fd_file_map, elem);
      if (cur_map->fd == fd) {
        list_remove (&cur_map->elem);
      }
    }
}

/* get the mapping struct according to fd from current thread's fd_list */
struct file* get_file(int fd)
{
  struct thread *cur_thread = thread_current();
  struct list_elem *e;
  struct fd_file_map *cur_map;

  for (e = list_begin (&cur_thread->fd_list); e != list_end (&cur_thread->fd_list);
       e = list_next (e))
    {
      struct fd_file_map* cur_map = list_entry (e, struct fd_file_map, elem);
      if (cur_map->fd == fd) {
        return cur_map->file;
      }
    }
  return NULL;
}


bool create (const char *file, unsigned initial_size)
{
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
};

bool remove (const char *file) {
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
};

int open (const char *file) {
  lock_acquire(&filesys_lock);
  struct file *opened = filesys_open(file);
  int fd;
  if (opened == NULL)
  {
    fd = -1;
  }
  else {
    fd = add_file(opened);
  }
  lock_release(&filesys_lock);
  return fd;

};

int filesize (int fd) {
  lock_acquire(&filesys_lock);
  struct file* aim = get_file(fd);
  int size;
  if (aim == NULL) {
    size =  -1;
  }
  else {
    size = file_length(aim);
  }
  lock_release(&filesys_lock);
  return size;

};

int read (int fd, void *buffer, unsigned size) {
  lock_acquire(&filesys_lock);
  int num_bytes_read;
  // TODO: Validate ptr
  //validate_addr()
  if (fd == 0) {
    int count = 0;
    while (count < size) {
      *(uint8_t *)(buffer + count) = input_getc();
    }
    num_bytes_read = count;
  }
  else {
    struct file * aim = get_file(fd);
    if (aim == NULL) {
      num_bytes_read = - 1;
    }
    else {
      num_bytes_read = file_read(aim, buffer, size);
    }
  }
  lock_release(&filesys_lock);
  return num_bytes_read;

};

int write (int fd, const void *buffer, unsigned size)
{
  lock_acquire(&filesys_lock);
  int num_bytes_written;
  // TODO: Validate ptr
  //validate_addr()
  if (fd == 1) {
    putbuf (buffer, size);
    num_bytes_written = size;
  }
  else {
    struct file * aim = get_file(fd);
    if (aim == NULL) {
      num_bytes_written = - 1;
    }
    else {
      num_bytes_written = file_write(aim, buffer, size);
    }
  }
  lock_release(&filesys_lock);
  return num_bytes_written;

}

void seek (int fd, unsigned position) {
  lock_acquire(&filesys_lock);
  struct file * aim = get_file(fd);
  if (aim != NULL) {
    file_seek(aim, position);
  }
  lock_release(&filesys_lock);
};

unsigned tell (int fd) {
  lock_acquire(&filesys_lock);
  struct file * aim = get_file(fd);
  unsigned told;
  if (aim == NULL) {
    //TODO: crash the user
  }
  else {
    told = file_tell(aim);
  }
  lock_release(&filesys_lock);
  return told;
};

void close (int fd) {
  lock_acquire(&filesys_lock);
  struct file * aim = get_file(fd);
  if (aim != NULL) {
    file_close(aim);
    remove_file(fd);
  }
  lock_release(&filesys_lock);
};
