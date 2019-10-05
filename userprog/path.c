#include <string.h>
#include "threads/thread.h"
#include "userprog/path.h"
#include "filesys/directory.h"

static char *path_trim (const char *path);

/* FILE_NAME should already been allocated */
bool
path_parse (const char *path, struct dir **parent_dir, char * file_name)
{
  struct dir *dir;
  char *trimmed_path;
  char *last_token = NULL;
  char *token, *save_ptr;

  if ( path==NULL || (strlen(path)==0) )
    return false;
  
  /* trim path */
  trimmed_path = path_trim(path);
  ASSERT(strlen(trimmed_path));
  
  /* set initial dir */
  if(*trimmed_path=='/')
  {
    dir = dir_open_root();
  }
  else
    dir = dir_reopen(thread_current()->cwd);

  for(token = strtok_r (trimmed_path, "/", &save_ptr); token!=NULL; token = strtok_r(NULL, "/", &save_ptr))
  {
    struct inode *new_dir_inode;

    /* if this is first token, don't update dir */
    if(last_token==NULL)
    {
      last_token = token;
      continue;
    }

    /* Update dir (skip '.' because it is itself */
    if( strcmp(".", last_token) )
    {
        /* if subdirectory doesn't exist, return false */
      if( !dir_lookup(dir, last_token, &new_dir_inode) 
          || !inode_is_dir(new_dir_inode) )
      {
        dir_close(dir);
        free(trimmed_path);
        return false;
      }
      
      dir_close(dir);
      dir = dir_open(new_dir_inode);  /* move to subdirectory */
    }

    last_token = token;
  }

  if(strlen(last_token) > 15)
  {
    dir_close(dir);
    free(trimmed_path);
    return false;
  }

  *parent_dir = dir;
  strlcpy(file_name, last_token, strlen(last_token)+1);

  free(trimmed_path);
  return true;
}

/*
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char *path_trim(const char *path);
void main()
{
  printf("\n%s\n", path_trim("/aa///"));
  printf("\n%s\n", path_trim("/aa"));
  printf("\n%s\n", path_trim("aa///"));
  printf("\n%s\n", path_trim("/aa/d/sd/"));
  printf("\n%s\n", path_trim("aa/s/////sdfa/"));
  printf("\n%s\n", path_trim("   "));
  printf("\n%s\n", path_trim("/"));
  printf("\n%s\n", path_trim("////"));
}
*/

/* !! ownership is on the caller */
static char *
path_trim (const char *path)
{
  enum state {WORD, SLASH};

  char *trimmed_path = malloc(strlen(path)+1);
  char *temp = (char *)path;
  char *ptr = trimmed_path;
  enum state state = WORD;

  while(*temp)
  {
    switch(state)
    {
      case WORD:
        if(*temp=='/')
        {
          state = SLASH;
          *ptr++ = *temp;
        }
        else
        {
          *ptr++ = *temp;
        }
        break;
      case SLASH:
        if(*temp=='/')
          break;
        else
        {
          state = WORD;
          *ptr++ = *temp;
        }
        break;
    }
    temp++;
  }

  if( ptr == trimmed_path )
    *ptr=0;
  else if( *(ptr-1) == '/' )
  {
    if( ptr != trimmed_path+1 )
      *(ptr-1) = 0;
    else
    {
      *ptr++ = '.';
      *ptr = 0;
    }
  }
  else
    *ptr = 0;
  
  return trimmed_path;
}
