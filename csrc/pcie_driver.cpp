#include <error.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "dma_xfer_utils.c"
#include "dmautils.h"

#include "pcie_driver.h"

static void xnl_dump_response(const char *resp) {
	printf("%s", resp);
}

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
		qdma_register_write(vf_perf, (pci_bus << 12) | (pci_dev << 4) | pf_start, 2, 0x50, cmptsz);
		qdma_register_write(vf_perf, (pci_bus << 12) | (pci_dev << 4) | pf_start, 2, 0x90, pkt_sz);
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
						last_fd = setup_thrd_env(&_info[base], is_new_fd);
					}
					_info[base].thread_id = base;
					base++;
        }
      }
    }
  }
}

int FpgaPcieDeviceAsyncIO::setup_thrd_env(struct io_info *_info, unsigned char is_new_fd) {
	int s;

	/* add queue */
	s = qdma_add_queue(vf_perf, _info);
	if (s < 0) {
		exit(1);
	}
	_info->q_added++;

	/* start queue */
	s = qdma_start_queue(vf_perf, _info);
	if (s < 0)
		exit(1);
	_info->q_started++;

	if (is_new_fd) {
		char node[25] = {'\0'};

		snprintf(node, 25, "/dev/%s", _info->q_name);
		_info->fd = open(node, O_RDWR);
		if (_info->fd < 0) {
			printf("Error: Cannot find %s\n", node);
			exit(1);
		}
	}

	s = ioctl(_info->fd, 0, &no_memcpy);
	if (s != 0) {
		printf("failed to set non memcpy\n");
		exit(1);
	}

	return _info->fd;
}

int FpgaPcieDeviceAsyncIO::qdma_add_queue(unsigned char is_vf, struct io_info *info) {
	struct xcmd_info xcmd;
	int ret;

	memset(&xcmd, 0, sizeof(struct xcmd_info));
	ret = qdma_prepare_q_add(&xcmd, is_vf, info);
	if (ret < 0)
		return ret;

	ret = qdma_q_add(&xcmd);
	if (ret < 0)
		printf("Q_ADD failed, ret :%d\n", ret);

	return ret;
}

int FpgaPcieDeviceAsyncIO::qdma_start_queue(unsigned char is_vf, struct io_info *info) {
	struct xcmd_info xcmd;
	int ret;

	memset(&xcmd, 0, sizeof(struct xcmd_info));
	ret = qdma_prepare_q_start(&xcmd, is_vf, info);
	if (ret < 0)
		return ret;

	ret = qdma_q_start(&xcmd);
	if (ret < 0)
		printf("Q_START failed, ret :%d\n", ret);

	return ret;
}

int FpgaPcieDeviceAsyncIO::qdma_prepare_q_add(struct xcmd_info *xcmd,
		unsigned char is_vf, struct io_info *info) {
	struct xcmd_q_parm *qparm;

	if (!xcmd) {
		printf("Error: Invalid Input Param\n");
		return -EINVAL;
	}

	qparm = &xcmd->req.qparm;

	xcmd->op = XNL_CMD_Q_ADD;
	xcmd->vf = is_vf;
	xcmd->if_bdf = info->pf;
	xcmd->log_msg_dump = xnl_dump_response;
	qparm->idx = info->qid;
	qparm->num_q = 1;

	if (info->mode == Q_MODE_MM)
		qparm->flags |= XNL_F_QMODE_MM;
	else if (info->mode == Q_MODE_ST)
		qparm->flags |= XNL_F_QMODE_ST;
	else
		return -EINVAL;

	if (info->dir == Q_DIR_H2C)
		qparm->flags |= XNL_F_QDIR_H2C;
	else if (info->dir == Q_DIR_C2H)
		qparm->flags |= XNL_F_QDIR_C2H;
	else
		return -EINVAL;

	qparm->sflags = qparm->flags;

	return 0;
}

int FpgaPcieDeviceAsyncIO::qdma_prepare_q_start(struct xcmd_info *xcmd, unsigned char is_vf, struct io_info *info) {
	struct xcmd_q_parm *qparm;
	unsigned int f_arg_set = 0;

	if (!xcmd) {
		printf("Error: Invalid Input Param\n");
		return -EINVAL;
	}
	qparm = &xcmd->req.qparm;

	xcmd->op = XNL_CMD_Q_START;
	xcmd->vf = is_vf;
	xcmd->if_bdf = info->pf;
	xcmd->log_msg_dump = xnl_dump_response;
	qparm->idx = info->qid;
	qparm->num_q = 1;
	f_arg_set |= 1 << QPARM_IDX;
	qparm->fetch_credit = Q_ENABLE_C2H_FETCH_CREDIT;

	if (info->mode == Q_MODE_MM)
		qparm->flags |= XNL_F_QMODE_MM;
	else if (info->mode == Q_MODE_ST)
		qparm->flags |= XNL_F_QMODE_ST;
	else
		return -EINVAL;
	f_arg_set |= 1 << QPARM_MODE;
	if (info->dir == Q_DIR_H2C)
		qparm->flags |= XNL_F_QDIR_H2C;
	else if (info->dir == Q_DIR_C2H)
		qparm->flags |= XNL_F_QDIR_C2H;
	else
		return -EINVAL;

	f_arg_set |= 1 << QPARM_DIR;
	qparm->qrngsz_idx = info->idx_rngsz;
	f_arg_set |= 1 << QPARM_RNGSZ_IDX;
	if ((info->dir == Q_DIR_C2H) && (info->mode == Q_MODE_ST)) {
		if (cmptsz)
			qparm->cmpt_entry_size = info->cmptsz;
		else
			qparm->cmpt_entry_size = XNL_ST_C2H_CMPT_DESC_SIZE_8B;
		f_arg_set |= 1 << QPARM_CMPTSZ;
		qparm->cmpt_tmr_idx = info->idx_tmr;
		f_arg_set |= 1 << QPARM_CMPT_TMR_IDX;
		qparm->cmpt_cntr_idx = info->idx_cnt;
		f_arg_set |= 1 << QPARM_CMPT_CNTR_IDX;

		if (!strcmp(info->trig_mode, "every"))
			qparm->cmpt_trig_mode = 1;
		else if (!strcmp(info->trig_mode, "usr_cnt"))
			qparm->cmpt_trig_mode = 2;
		else if (!strcmp(info->trig_mode, "usr"))
			qparm->cmpt_trig_mode = 3;
		else if (!strcmp(info->trig_mode, "usr_tmr"))
			qparm->cmpt_trig_mode=4;
		else if (!strcmp(info->trig_mode, "cntr_tmr"))
			qparm->cmpt_trig_mode=5;
		else if (!strcmp(info->trig_mode, "dis"))
			qparm->cmpt_trig_mode = 0;
		else {
			printf("Error: unknown q trigmode %s.\n", info->trig_mode);
			return -EINVAL;
		}
		f_arg_set |= 1 << QPARM_CMPT_TRIG_MODE;
		if (pfetch_en)
			qparm->flags |= XNL_F_PFETCH_EN;
	}

	if (info->mode == Q_MODE_MM) {
		qparm->mm_channel = info->mm_chnl;
		f_arg_set |= 1 <<QPARM_MM_CHANNEL;
	}


	if ((info->dir == Q_DIR_H2C) && (info->mode == Q_MODE_MM)) {
		if (keyhole_en) {
			qparm->aperture_sz = info->aperture_sz;
			f_arg_set |= 1 << QPARM_KEYHOLE_EN;
		}
	}

	qparm->flags |= (XNL_F_CMPL_STATUS_EN | XNL_F_CMPL_STATUS_ACC_EN |
			XNL_F_CMPL_STATUS_PEND_CHK | XNL_F_CMPL_STATUS_DESC_EN |
			XNL_F_FETCH_CREDIT);

	qparm->sflags = f_arg_set;
	return 0;
}

int FpgaPcieDeviceAsyncIO::qdma_register_write(unsigned char is_vf,
		unsigned int pf, int bar, unsigned long reg,
		unsigned long value) {
	struct xcmd_info xcmd;
	struct xcmd_reg *regcmd;
	int ret;

	memset(&xcmd, 0, sizeof(struct xcmd_info));

	regcmd = &xcmd.req.reg;
	xcmd.op = XNL_CMD_REG_WRT;
	xcmd.vf = is_vf;
	xcmd.if_bdf = pf;
	xcmd.log_msg_dump = xnl_dump_response;
	regcmd->bar = bar;
	regcmd->reg = reg;
	regcmd->val = value;
	regcmd->sflags = XCMD_REG_F_BAR_SET |
		XCMD_REG_F_REG_SET |
		XCMD_REG_F_VAL_SET;

	ret = qdma_reg_write(&xcmd);
	if (ret < 0)
		printf("QDMA_REG_WRITE Failed, ret :%d\n", ret);

	return ret;
}
