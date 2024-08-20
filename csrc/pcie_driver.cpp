#include <error.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "dma_xfer_utils.c"
#include "pcie_driver.h"

int FpgaPcieDevice::open_pcie_device(const char *devname) {
    fpga_fd = open(devname, O_RDWR);
    memcpy(fpga_name, devname, sizeof(devname));
    if (fpga_fd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n", devname, fpga_fd);
		perror("open device");
		return -EINVAL;
	}
    return 0;
}

int FpgaPcieDevice::read_pcie_buffer(char *buffer, uint64_t size, uint64_t addr) {
    if (read_to_buffer(fpga_name, fpga_fd, buffer, size, addr) < 0) {
        assert(0);
    }
    return 0;
}

void FpgaPcieDeviceAsyncIO::aioInit() {
    // edit max aio
    snprintf(aio_max_nr_cmd, 100, "echo %u > /proc/sys/fs/aio-max-nr", aio_max_nr);
    system(aio_max_nr_cmd);

    // parama init
    mode = Q_MODE_ST;
    dir = Q_DIR_C2H;
    pf_start = 0;
    num_pf = 1;
    q_start = 0;
    num_q = 8;
    idx_tmr = 9;
    idx_cnt = 7;
    dump_en = 1;
    pfetch_en = 1;
    cmptsz = 1;
    idx_rngsz = 0;
    tsecs = 30;
    num_thrds_per_q = 2;
    num_pkts = 64;
    mm_chnl = 0;
    pkt_sz = 4096;
    pci_bus = 0x01;
    pci_dev = 0x00;
    marker_en = 1;

    // Set trigmode
    strncpy(trigmode, "cntr_tmr", sizeof(trigmode) - 1);
    trigmode[sizeof(trigmode) - 1] = '\0';

    // Global ring size information is obtained
    char rng_sz_path[200];
    char rng_sz[100] = {'\0'};
    int rng_sz_fd, ret;
    dmactl_dev_prefix_str = pf_dmactl_prefix_str;
    snprintf(rng_sz_path, sizeof(rng_sz_path), 
             "dma-ctl %s%05x global_csr | grep \"Global Ring\"| cut -d \":\" -f 2 > glbl_rng_sz",
             dmactl_dev_prefix_str, (pci_bus << 12) | (pci_dev << 4) | pf_start);
    system(rng_sz_path);
    snprintf(rng_sz_path, sizeof(rng_sz_path), "glbl_rng_sz");

    rng_sz_fd = open(rng_sz_path, O_RDONLY);
    if (rng_sz_fd < 0) {
        printf("Could not open %s\n", rng_sz_path);
        exit(1);
    }

    ret = read(rng_sz_fd, &rng_sz[1], sizeof(rng_sz) - 2);
    if (ret < 0) {
        printf("Error: Could not read the file\n");
        close(rng_sz_fd);
        exit(1);
    }
    close(rng_sz_fd);
	rng_sz[0] = ' ';
	rng_sz[strlen(rng_sz)] = ' ';

	printf("dmautils(%u) threads\n", num_thrds);
	child_pid_lst = calloc(num_thrds, sizeof(int));
	base_pid = getpid();
	child_pid_lst[0] = base_pid;
	for (int i = 1; i < num_thrds; i++) {
		if (getpid() == base_pid)
			child_pid_lst[i] = fork();
		else
			break;
	}

    // Creating thread information
    int dir_factor = (dir == Q_DIR_BI) ? 2 : 1;
    num_thrds = num_pf * num_q * dir_factor * num_thrds_per_q;

    create_thread_info();
    if (stm_mode) {
		free(pipe_tdest_lst);
		free(pipe_slr_id_lst);
		free(pipe_flow_id_lst);
		free(pipe_gl_max_lst);
	}
}

void FpgaPcieDeviceAsyncIO::create_thread_info(void) {
	unsigned int base = 0;
	unsigned int dir_factor = (dir == Q_DIR_BI) ? 2 : 1;
	unsigned int q_ctrl = 1;
	unsigned int i, j, k;
	struct io_info *_info;
	int last_fd = -1;
	unsigned char is_new_fd = 1;

  if ((shmid = shmget(IPC_PRIVATE, num_thrds * sizeof(struct io_info), IPC_CREAT | 0666)) < 0) {
		error(-1, errno, "smget returned -1\n");
	}
	if ((q_lst_stop_mid = shmget(IPC_PRIVATE, dir_factor * num_q * num_pf, IPC_CREAT | 0666)) < 0) {
		error(-1, errno, "smget returned -1\n");
	}
	if ((q_lst_stop = (unsigned char *) shmat(q_lst_stop_mid, NULL, 0)) == (unsigned char *) -1) {
		error(-1, errno, "Process shmat returned NULL\n");
	}
	memset(q_lst_stop, 0 , num_q);
	if (shmdt(q_lst_stop) == -1) {
	    error(-1, errno, "shmdt returned -1\n");
	}
	if ((_info = (struct io_info *) shmat(shmid, NULL, 0)) == (struct io_info *) -1) {
		error(-1, errno, "Process shmat returned NULL\n");
	}
	if ((io_exit_id = shmget(IPC_PRIVATE,sizeof(unsigned int), IPC_CREAT | 0666)) < 0) {
		error(-1, errno, "smget returned -1\n");
	}
	if ((io_exit = (unsigned int *) shmat(io_exit_id, NULL, 0)) == (unsigned int *) -1) {
		error(-1, errno, "Process shmat returned NULL\n");
	}

	if ((mode == Q_MODE_ST) && (dir != Q_DIR_H2C)) {
		error(-1, errno, "Need Vf support");
		// qdma_register_write(vf_perf, (pci_bus << 12) | (pci_dev << 4) | pf_start, 2, 0x50, cmptsz);
		// qdma_register_write(vf_perf, (pci_bus << 12) | (pci_dev << 4) | pf_start, 2, 0x90, pkt_sz);
		usleep(1000);
	}
	base = 0;

	for (k = 0; k < num_pf; k++) {
		for (i = 0 ; i < num_q; i++) {
			q_ctrl = 1;
			for (j = 0; j < num_thrds_per_q; j++) {
				is_new_fd = 1;
        if (dir != Q_DIR_H2C) {
					snprintf(_info[base].q_name, 20, "%s%02x%02x%01x-%s-%u",
					dmactl_dev_prefix_str, pci_bus, pci_dev, pf_start+k, (mode == Q_MODE_MM) ? "MM" : "ST", q_start + i);
					_info[base].exit_check_count = 0;
					_info[base].dir = Q_DIR_C2H;
					_info[base].mode = mode;
					_info[base].idx_rngsz = idx_rngsz;
					_info[base].pf = (pci_bus << 12) | (pci_dev << 4) | (pf_start + k);
					_info[base].qid = q_start + i;
					_info[base].q_ctrl = q_ctrl;
					_info[base].pkt_burst = num_pkts;
					if(mm_chnl == MM_CHANNEL_INTERLEAVE)
						_info[base].mm_chnl = _info[base].qid % 2;
					else
						_info[base].mm_chnl = mm_chnl;
					if(mm_chnl == MM_CHANNEL_INTERLEAVE && _info[base].mm_chnl )
						_info[base].offset = offset_ch1;
					else
						_info[base].offset = offset;

					_info[base].pkt_sz = pkt_sz;

					if (_info[base].mode == Q_MODE_ST) {
						_info[base].pfetch_en = pfetch_en;
						_info[base].idx_cnt = idx_cnt;
						_info[base].idx_tmr = idx_tmr;
						_info[base].cmptsz = cmptsz;
						strncpy(_info[base].trig_mode, trigmode, 10);
						if (stm_mode) {
							_info[base].stm_mode = stm_mode;
							_info[base].pipe_gl_max = pipe_gl_max_lst[(k*num_q) + i];
							_info[base].pipe_flow_id = pipe_flow_id_lst[(k*num_q) + i];
							_info[base].pipe_slr_id = pipe_slr_id_lst[(k*num_q) + i];
							_info[base].pipe_tdest = pipe_tdest_lst[(k*num_q) + i];
						}
					}
					sem_init(&_info[base].llock, 0, 1);
					_info[base].fd = last_fd;
					if (q_ctrl != 0) {
						//last_fd = setup_thrd_env(&_info[base], is_new_fd);
					}
					_info[base].thread_id = base;
					base++;
        }
      }
    }
  }
}