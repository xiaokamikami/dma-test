#ifndef __FPGA_PCIE_DRIVER_H__
#define __FPGA_PCIE_DRIVER_H__

#define PCI_DUMP_CMD_LEN 128
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <semaphore.h>
#include </usr/include/pthread.h>

#include "pcie_mempool.h"

#define QDMA_GLBL_MAX_ENTRIES  (16)

enum q_mode {
	Q_MODE_MM,
	Q_MODE_ST,
	Q_MODES
};

enum q_dir {
	Q_DIR_H2C,
	Q_DIR_C2H,
	Q_DIR_BI,
	Q_DIRS
};

enum mm_channel_ctrl {
	MM_CHANNEL_0, /*All Qs are assigned to channel 0*/
	MM_CHANNEL_1, /*All Qs are assigned to channel 1*/
	MM_CHANNEL_INTERLEAVE /*Odd queues are assigned to ch 1 and even Qs are assigned to channel 0*/
};

// IO's event queue
struct io_info {
	unsigned int num_req_submitted;
	unsigned int num_req_completed;
	unsigned int num_req_completed_in_time;
	int exit_check_count;
	struct list_head *head;
	struct list_head *tail;
	sem_t llock;
	int pid;
	pthread_t evt_id;
	char q_name[20];
	char trig_mode[10];
	unsigned char q_ctrl;
	unsigned int q_added;
	unsigned int q_started;
	unsigned int q_wait_for_stop;
	int fd;
	unsigned int pf;
	unsigned int qid;
	enum q_mode mode;
	enum q_dir dir;
	unsigned int idx_tmr;
	unsigned int idx_cnt;
	unsigned int idx_rngsz;
	unsigned int pfetch_en;
	unsigned int pkt_burst;
	unsigned int pkt_sz;
	unsigned int cmptsz;
	unsigned int stm_mode;
	unsigned int pipe_gl_max;
	unsigned int pipe_flow_id;
	unsigned int pipe_slr_id;
	unsigned int pipe_tdest;
	unsigned int mm_chnl;
	int keyhole_en;
	unsigned int aperture_sz;
	unsigned long int offset;

	unsigned int thread_id;
};



class FpgaPcieDevice {
public:
    unsigned int pci_bus = 0;
    unsigned int pci_dev = 0;

    FpgaPcieDevice(const char *fpga_name) {
        open_pcie_device(fpga_name);
    };
    ~FpgaPcieDevice() {
        if (fpga_fd > 0) {
            close(fpga_fd);
        }
    };

    int open_pcie_device(const char *devname);
    int read_pcie_buffer(char *buffer, uint64_t size, uint64_t addr);
    void prep_pci_dump(void) {
        char *pci_dump = (char *)malloc(PCI_DUMP_CMD_LEN);
        memset(pci_dump, '\0', PCI_DUMP_CMD_LEN);
        snprintf(pci_dump, PCI_DUMP_CMD_LEN, "lspci -s %02x:%02x -vvv", pci_bus, pci_dev);
    }
private:
    size_t rc = 0;
    int fpga_fd = -1;
    char fpga_name[128]={};
};

class FpgaPcieDeviceAsyncIO {
public:
    unsigned int aio_max_nr = 0xFFFFFFFF;
	char aio_max_nr_cmd[100] = {'\0'};

    FpgaPcieDeviceAsyncIO(const char *fpga_name) {
        aioInit();

    };
    ~FpgaPcieDeviceAsyncIO() {

    };

    void aioInit();
    void create_thread_info(void);
	int setup_thrd_env(struct io_info *_info, unsigned char is_new_fd);

	int qdma_add_queue(unsigned char is_vf, struct io_info *info);
	int qdma_start_queue(unsigned char is_vf, struct io_info *info);
	int qdma_prepare_q_add(struct xcmd_info *xcmd, unsigned char is_vf, struct io_info *info);
	int qdma_prepare_q_start(struct xcmd_info *xcmd, unsigned char is_vf, struct io_info *info);
    int qdma_register_write(unsigned char is_vf,
		unsigned int pf, int bar, unsigned long reg,
		unsigned long value);

    size_t asyncRead(void* buffer, size_t size, uint64_t offset);
    size_t asyncWrite(const void* buffer, size_t size, uint64_t offset);

private:
    FpgaPcieMemPool dmaMemPool;
	struct io_info *info = NULL;
	enum q_mode mode;
	enum q_dir dir;

	/* For MM Channel =0 or 1 , offset is used for both MM Channels */
	unsigned long int offset = 0;
	/*In MM Channel interleaving offset_ch1 is the offset used for Channel 1*/
	unsigned long int offset_ch1 = 0;

	unsigned char *q_lst_stop = NULL;
	char *dmactl_dev_prefix_str = NULL;
	char *pf_dmactl_prefix_str = "qdma";
	char trigmode[10];

	unsigned int *io_exit = 0;
	int io_exit_id;

	int *child_pid_lst = NULL;
	int q_lst_stop_mid;
	int keyhole_en;
	int base_pid;
	int shmid;

	unsigned int *pipe_gl_max_lst = NULL;
	unsigned int *pipe_flow_id_lst = NULL;
	unsigned int *pipe_slr_id_lst = NULL;
	unsigned int *pipe_tdest_lst = NULL;

	unsigned int glbl_rng_sz[QDMA_GLBL_MAX_ENTRIES];
	unsigned int mm_chnl = 0;
	unsigned int force_exit = 0;
	unsigned int num_q = 0;
	unsigned int pkt_sz = 0;
	unsigned int num_pkts;
	unsigned int num_pf = 0;
	unsigned int pf_start = 0;
	unsigned int q_start = 0;
	unsigned int idx_rngsz = 0;
	unsigned int idx_tmr = 0;
	unsigned int idx_cnt = 0;
	unsigned int pfetch_en = 0;
	unsigned int cmptsz = 0;
	unsigned int no_memcpy = 1;
	unsigned int stm_mode = 0;

	unsigned int dump_en = 0;
	unsigned int marker_en = 1;
	unsigned int tsecs = 0;
	unsigned int num_thrds = 0;
	unsigned int num_thrds_per_q = 1;
	unsigned int pci_bus = 0;
	unsigned int pci_dev = 0;
	unsigned int vf_perf = 0;
};


#endif
