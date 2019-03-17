#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

// Struct to store info about each proccess
typedef struct proc_data {
  int proc_id;
  char *proc_name;
  int contig_pages;
  int noncontig_pages;
  struct proc_data *next;
} proc_data;

// Struct to store global variables.
typedef struct data_head {
  proc_data *head;
  int total_cnt;
  int total_ncnt;
} data_head;

// Initial Global struct that stores all relevent data.
data_head global_head;
int proc_init(void);
static void create_list(void);
unsigned long virt2phys(struct mm_struct*, unsigned long);
static int proc_show(struct seq_file *, void *);
void print2log(void);
static int proc_open(struct inode *, struct file *);
void freeList(void);
void proc_cleanup(void);

// proc_fops struct. proc_open must be defined and created before its calledfrom
static const struct file_operations proc_fops = {
  .owner = THIS_MODULE,
  .open = proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};

/**
 *  Called by module_init function to initial the kernal process.
 * 
 * Creates 
 **/
int proc_init () {
  create_list();
  proc_create("proc_report", 0, NULL, &proc_fops);
  print2log();
  return 0;
}

static void create_list() {
  struct task_struct *task;
  struct vm_area_struct *vma;
  unsigned long prev, vpage;
  proc_data *proc_temp;

  //
  proc_temp = kmalloc(sizeof(proc_data), GFP_KERNEL);
  proc_temp->contig_pages = 0;
  proc_temp->noncontig_pages = 0;
  proc_temp->next = NULL;
  
  // save the location of the first node of the list into the global variable.
  global_head.head = proc_temp;
  prev = 0;
  vma = 0;

  for_each_process(task) {
    if(task->pid > 650) {
      proc_temp->proc_id = task->pid;
      proc_temp->proc_name = task->comm;
      vma = 0;

      if (task->mm && task->mm->mmap) {
        for (vma = task->mm->mmap; vma; vma = vma->vm_next) {
          for (vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
            unsigned long physical_page_addr = virt2phys(task->mm, vpage);
            if(physical_page_addr) {
              (physical_page_addr == prev + PAGE_SIZE)? proc_temp->contig_pages++ : proc_temp->noncontig_pages++;
              prev = physical_page_addr;
            }
          }
        }
        global_head.total_cnt += proc_temp->contig_pages;
        global_head.total_ncnt += proc_temp->noncontig_pages;  
      }
      proc_temp->next = kmalloc(sizeof(proc_data), GFP_KERNEL);
      proc_temp = proc_temp->next;
      proc_temp->contig_pages = 0;
      proc_temp->noncontig_pages = 0;
    }
  } 
  proc_temp->next = NULL;
  proc_temp = NULL;
  kfree(proc_temp);
}

unsigned long virt2phys(struct mm_struct *mm, unsigned long vpage) {
  pgd_t *pgd;
  p4d_t *p4d;
  pud_t *pud;
  pmd_t *pmd;
  pte_t *pte;
  struct page *page;
  unsigned long physical_page_addr;
  
  pgd = pgd_offset(mm, vpage);
  if(pgd_none(*pgd) || pgd_bad(*pgd)) return 0;
  p4d = p4d_offset(pgd, vpage);
  if(p4d_none(*p4d) || p4d_bad(*p4d)) return 0;
  pud = pud_offset(p4d, vpage);
  if(pud_none(*pud) || pud_bad(*pud)) return 0;
  pmd = pmd_offset(pud, vpage);
  if(pmd_none(*pmd) || pmd_bad(*pmd)) return 0;
  if(!(pte = pte_offset_map(pmd, vpage))) return 0;
  if(!(page = pte_page(*pte)))  return 0;
  physical_page_addr = page_to_phys(page);
  pte_unmap(pte);
  return physical_page_addr;
}

/**
 * Using the gloabl_head variable, it prints the report of the data collected to /proc/proc_report.
 **/
static int proc_show(struct seq_file *m, void *v) {
  proc_data *proc_temp = global_head.head;
  seq_printf(m, "PROCESS REPORT:\n");
  seq_printf(m, "proc_id,proc_name,contig_pages,noncontig_pages,total_pages\n");
  while(proc_temp->next) {
    seq_printf(m, "%d,%s,%d,%d,%d\n", proc_temp->proc_id, proc_temp->proc_name, proc_temp->contig_pages, proc_temp->noncontig_pages, proc_temp->contig_pages + proc_temp->noncontig_pages);
    proc_temp = proc_temp->next;  
  }
  seq_printf(m, "TOTALS,,%d,%d,%d\n", global_head.total_cnt, global_head.total_ncnt, global_head.total_cnt + global_head.total_ncnt);
  return 0;
}
/**
 * Using the gloabl_head variable, it prints the report of the data collected to /var/log/syslog files.
 **/
void print2log() {
  proc_data *proc_temp = global_head.head;
  printk(KERN_INFO "PROCESS REPORT:\n");
  printk(KERN_INFO "proc_id,proc_name,contig_pages,noncontig_pages,total_pages\n");
  while(proc_temp->next) {
    printk(KERN_INFO "%d,%s,%d,%d,%d\n", proc_temp->proc_id, proc_temp->proc_name, proc_temp->contig_pages, proc_temp->noncontig_pages, proc_temp->contig_pages + proc_temp->noncontig_pages);
    proc_temp = proc_temp->next;  
  }
  printk(KERN_INFO "TOTALS,,%d,%d,%d\n", global_head.total_cnt, global_head.total_ncnt, global_head.total_cnt + global_head.total_ncnt);
}

static int proc_open(struct inode *inode, struct file *file) {
  return single_open(file, proc_show, NULL);
}

void freeList() {
  proc_data *proc_temp;  
  while(global_head.head) {
    proc_temp = global_head.head->next;
    kfree(global_head.head);
    global_head.head = proc_temp;
  }
}

void proc_cleanup(void) {
  remove_proc_entry("proc_report", NULL);
  freeList();
  printk(KERN_INFO "procReport: performing cleanup of module\n");
}

MODULE_LICENSE("GPL");
module_init(proc_init);
module_exit(proc_cleanup);

