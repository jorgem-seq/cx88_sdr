// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020 Jorge Maidana <jorgem.seq@gmail.com>
 *
 * This driver is a derivative of:
 *
 * device driver for Conexant 2388x based TV cards
 * Copyright (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 * device driver for Conexant 2388x based TV cards
 * Copyright (c) 2005-2006 Mauro Carvalho Chehab <mchehab@kernel.org>
 *
 * cxadc.c - CX2388x ADC DMA driver for Linux 2.6.18 version 0.3
 * Copyright (c) 2005-2007 Hew How Chee <how_chee@yahoo.com>
 *
 * cxadc.c - CX2388x ADC DMA driver for Linux 3.x version 0.5
 * Copyright (c) 2013-2015 Chad Page <Chad.Page@gmail.com>
 */

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "cx88_sdr.h"

#define CX88SDR_V4L2_NAME		"CX2388x SDR V4L2"

/* The base for the cx88_sdr driver controls. Total of 16 controls are reserved
 * for this driver */
#ifndef V4L2_CID_USER_CX88SDR_BASE
#define V4L2_CID_USER_CX88SDR_BASE	(V4L2_CID_USER_BASE + 0x1f10)
#endif

enum {
	V4L2_CID_CX88SDR_INPUT	= (V4L2_CID_USER_CX88SDR_BASE + 0),
};

struct cx88sdr_fh {
	struct v4l2_fh fh;
	struct cx88sdr_dev *dev;
};

static const struct v4l2_frequency_band cx88sdr_bands_ru08[] = {
	[CX88SDR_BAND_00] = {
		.tuner		= 0,
		.type		= V4L2_TUNER_SDR,
		.index		= 0,
		.capability	= (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS),
		.rangelow	= (CX88SDR_XTAL_FREQ / 2),
		.rangehigh	= (CX88SDR_XTAL_FREQ / 2),
	},
	[CX88SDR_BAND_01] = { /* Default */
		.tuner		= 0,
		.type		= V4L2_TUNER_SDR,
		.index		= 1,
		.capability	= (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS),
		.rangelow	= (CX88SDR_XTAL_FREQ),
		.rangehigh	= (CX88SDR_XTAL_FREQ),
	},
	[CX88SDR_BAND_02] = {
		.tuner		= 0,
		.type		= V4L2_TUNER_SDR,
		.index		= 2,
		.capability	= (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS),
		.rangelow	= (CX88SDR_XTAL_FREQ * 5 / 4),
		.rangehigh	= (CX88SDR_XTAL_FREQ * 5 / 4),
	},
};

static const struct v4l2_frequency_band cx88sdr_bands_ru16[] = {
	[CX88SDR_BAND_00] = {
		.tuner		= 0,
		.type		= V4L2_TUNER_SDR,
		.index		= 0,
		.capability	= (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS),
		.rangelow	= (CX88SDR_XTAL_FREQ / 4),
		.rangehigh	= (CX88SDR_XTAL_FREQ / 4),
	},
	[CX88SDR_BAND_01] = {
		.tuner		= 0,
		.type		= V4L2_TUNER_SDR,
		.index		= 1,
		.capability	= (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS),
		.rangelow	= (CX88SDR_XTAL_FREQ / 2),
		.rangehigh	= (CX88SDR_XTAL_FREQ / 2),
	},
	[CX88SDR_BAND_02] = {
		.tuner		= 0,
		.type		= V4L2_TUNER_SDR,
		.index		= 2,
		.capability	= (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS),
		.rangelow	= (CX88SDR_XTAL_FREQ * 5 / 8),
		.rangehigh	= (CX88SDR_XTAL_FREQ * 5 / 8),
	},
};

static int cx88sdr_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct cx88sdr_dev *dev = container_of(vdev, struct cx88sdr_dev, vdev);
	struct cx88sdr_fh *fh;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return -ENOMEM;

	v4l2_fh_init(&fh->fh, vdev);

	fh->dev = dev;
	file->private_data = &fh->fh;
	v4l2_fh_add(&fh->fh);

	dev->start_page = ctrl_ioread32(dev, MO_VBI_GPCNT) - 1;
	ctrl_iowrite32(dev, MO_PCI_INTMSK, 1);
	return 0;
}

static int cx88sdr_release(struct file *file)
{
	struct v4l2_fh *vfh = file->private_data;
	struct cx88sdr_fh *fh = container_of(vfh, struct cx88sdr_fh, fh);
	struct cx88sdr_dev *dev = fh->dev;

	ctrl_iowrite32(dev, MO_PCI_INTMSK, 0);

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	kfree(fh);
	return 0;
}

static ssize_t cx88sdr_read(struct file *file, char __user *buf, size_t size,
			    loff_t *pos)
{
	struct v4l2_fh *vfh = file->private_data;
	struct cx88sdr_fh *fh = container_of(vfh, struct cx88sdr_fh, fh);
	struct cx88sdr_dev *dev = fh->dev;
	ssize_t result = 0;
	uint32_t page, page_cnt;

	page = (dev->start_page + ((*pos % VBI_DMA_SIZE) >> PAGE_SHIFT)) %
		VBI_DMA_PAGES;

retry:
	page_cnt = ctrl_ioread32(dev, MO_VBI_GPCNT);
	page_cnt = (!page_cnt) ? (VBI_DMA_PAGES - 1) : (page_cnt - 1);

	if ((page == page_cnt) && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	while (size && page != page_cnt) {
		u32 len;

		/* Handle partial pages */
		len = (*pos % PAGE_SIZE) ? (PAGE_SIZE - (*pos % PAGE_SIZE)) : PAGE_SIZE;
		if (len > size)
			len = size;

		if (copy_to_user(buf, dev->dma_buf_pages[page] + (*pos % PAGE_SIZE), len))
			return -EFAULT;

		memset(dev->dma_buf_pages[page] + (*pos % PAGE_SIZE), 0, len);

		result += len;
		buf    += len;
		*pos   += len;
		size   -= len;
		page    = (dev->start_page + ((*pos % VBI_DMA_SIZE) >> PAGE_SHIFT)) %
			   VBI_DMA_PAGES;
	}

	if (size && !(file->f_flags & O_NONBLOCK))
		goto retry;

	return result;
}

static __poll_t cx88sdr_poll(struct file *file, struct poll_table_struct *wait)
{
	return (EPOLLIN | EPOLLRDNORM | v4l2_ctrl_poll(file, wait));
}

static const struct v4l2_file_operations cx88sdr_fops = {
	.owner		= THIS_MODULE,
	.open		= cx88sdr_open,
	.release	= cx88sdr_release,
	.read		= cx88sdr_read,
	.poll		= cx88sdr_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static int cx88sdr_querycap(struct file *file, void __always_unused *priv,
			    struct v4l2_capability *cap)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s", pci_name(dev->pdev));
	strscpy(cap->card, CX88SDR_DRV_NAME, sizeof(cap->card));
	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	return 0;
}

static int cx88sdr_enum_fmt_sdr(struct file __always_unused *file,
				void __always_unused *priv,
				struct v4l2_fmtdesc *f)
{
	switch (f->index) {
	case 0:
		f->pixelformat = V4L2_SDR_FMT_RU8;
		break;
	case 1:
		f->pixelformat = V4L2_SDR_FMT_RU16LE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx88sdr_try_fmt_sdr(struct file *file, void __always_unused *priv,
			       struct v4l2_format *f)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	switch (f->fmt.sdr.pixelformat) {
	case V4L2_SDR_FMT_RU8:
		f->fmt.sdr.buffersize = dev->buffersize;
		break;
	case V4L2_SDR_FMT_RU16LE:
		f->fmt.sdr.buffersize = dev->buffersize;
		break;
	default:
		f->fmt.sdr.pixelformat = V4L2_SDR_FMT_RU8;
		f->fmt.sdr.buffersize = dev->buffersize;
		break;
	}
	return 0;
}

static int cx88sdr_g_fmt_sdr(struct file *file, void __always_unused *priv,
			     struct v4l2_format *f)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	f->fmt.sdr.pixelformat = dev->pixelformat;
	f->fmt.sdr.buffersize = dev->buffersize;
	return 0;
}

static int cx88sdr_s_fmt_sdr(struct file *file, void __always_unused *priv,
			     struct v4l2_format *f)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));

	switch (f->fmt.sdr.pixelformat) {
	case V4L2_SDR_FMT_RU8:
		dev->pixelformat = V4L2_SDR_FMT_RU8;
		f->fmt.sdr.buffersize = dev->buffersize;
		break;
	case V4L2_SDR_FMT_RU16LE:
		dev->pixelformat = V4L2_SDR_FMT_RU16LE;
		f->fmt.sdr.buffersize = dev->buffersize;
		break;
	default:
		dev->pixelformat = V4L2_SDR_FMT_RU8;
		f->fmt.sdr.pixelformat = V4L2_SDR_FMT_RU8;
		f->fmt.sdr.buffersize = dev->buffersize;
		break;
	}
	return cx88sdr_adc_fmt_set(dev);
}

static int cx88sdr_g_tuner(struct file *file, void __always_unused *priv,
			   struct v4l2_tuner *t)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	if (t->index > 0)
		return -EINVAL;

	switch (dev->pixelformat) {
	case V4L2_SDR_FMT_RU8:
		t->rangelow  = cx88sdr_bands_ru08[CX88SDR_BAND_00].rangelow;
		t->rangehigh = cx88sdr_bands_ru08[CX88SDR_BAND_02].rangehigh;
		break;
	case V4L2_SDR_FMT_RU16LE:
		t->rangelow  = cx88sdr_bands_ru16[CX88SDR_BAND_00].rangelow;
		t->rangehigh = cx88sdr_bands_ru16[CX88SDR_BAND_02].rangehigh;
		break;
	default:
		return -EINVAL;
	}
	strscpy(t->name, "ADC: CX2388x SDR", sizeof(t->name));
	t->type = V4L2_TUNER_SDR;
	t->capability = (V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS);
	return 0;
}

static int cx88sdr_s_tuner(struct file __always_unused *file,
			   void __always_unused *priv,
			   const struct v4l2_tuner *t)
{
	if (t->index > 0)
		return -EINVAL;

	return 0;
}

static int cx88sdr_enum_freq_bands(struct file *file, void __always_unused *priv,
				   struct v4l2_frequency_band *band)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	if (band->tuner > 0 || band->index > CX88SDR_BAND_02)
		return -EINVAL;

	switch (dev->pixelformat) {
	case V4L2_SDR_FMT_RU8:
		*band = cx88sdr_bands_ru08[band->index];
		break;
	case V4L2_SDR_FMT_RU16LE:
		*band = cx88sdr_bands_ru16[band->index];
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx88sdr_g_frequency(struct file *file, void __always_unused *priv,
			       struct v4l2_frequency *f)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	if (f->tuner > 0)
		return -EINVAL;

	switch (dev->pixelformat) {
	case V4L2_SDR_FMT_RU8:
		f->frequency = cx88sdr_bands_ru08[dev->sdr_band].rangelow;
		break;
	case V4L2_SDR_FMT_RU16LE:
		f->frequency = cx88sdr_bands_ru16[dev->sdr_band].rangelow;
		break;
	default:
		return -EINVAL;
	}
	f->type = V4L2_TUNER_SDR;
	return 0;
}

static int cx88sdr_s_frequency(struct file *file, void __always_unused *priv,
			       const struct v4l2_frequency *f)
{
	struct cx88sdr_dev *dev = video_drvdata(file);

	if (f->tuner > 0 || f->type != V4L2_TUNER_SDR)
		return -EINVAL;

	switch (dev->pixelformat) {
	case V4L2_SDR_FMT_RU8:
		if      (dev->sdr_band != CX88SDR_BAND_00 &&
			 f->frequency   < cx88sdr_bands_ru08[CX88SDR_BAND_01].rangelow)
			dev->sdr_band   = CX88SDR_BAND_00;
		else if (dev->sdr_band != CX88SDR_BAND_01 &&
			 f->frequency   > cx88sdr_bands_ru08[CX88SDR_BAND_00].rangehigh &&
			 f->frequency   < cx88sdr_bands_ru08[CX88SDR_BAND_02].rangelow)
			dev->sdr_band   = CX88SDR_BAND_01;
		else if (dev->sdr_band != CX88SDR_BAND_02 &&
			 f->frequency   > cx88sdr_bands_ru08[CX88SDR_BAND_01].rangehigh)
			dev->sdr_band   = CX88SDR_BAND_02;
		break;
	case V4L2_SDR_FMT_RU16LE:
		if      (dev->sdr_band != CX88SDR_BAND_00 &&
			 f->frequency   < cx88sdr_bands_ru16[CX88SDR_BAND_01].rangelow)
			dev->sdr_band   = CX88SDR_BAND_00;
		else if (dev->sdr_band != CX88SDR_BAND_01 &&
			 f->frequency   > cx88sdr_bands_ru16[CX88SDR_BAND_00].rangehigh &&
			 f->frequency   < cx88sdr_bands_ru16[CX88SDR_BAND_02].rangelow)
			dev->sdr_band   = CX88SDR_BAND_01;
		else if (dev->sdr_band != CX88SDR_BAND_02 &&
			 f->frequency   > cx88sdr_bands_ru16[CX88SDR_BAND_01].rangehigh)
			dev->sdr_band   = CX88SDR_BAND_02;
		break;
	default:
		return -EINVAL;
	}
	return cx88sdr_adc_fmt_set(dev);
}

static const struct v4l2_ioctl_ops cx88sdr_ioctl_ops = {
	.vidioc_querycap		= cx88sdr_querycap,
	.vidioc_enum_fmt_sdr_cap	= cx88sdr_enum_fmt_sdr,
	.vidioc_try_fmt_sdr_cap		= cx88sdr_try_fmt_sdr,
	.vidioc_g_fmt_sdr_cap		= cx88sdr_g_fmt_sdr,
	.vidioc_s_fmt_sdr_cap		= cx88sdr_s_fmt_sdr,
	.vidioc_g_tuner			= cx88sdr_g_tuner,
	.vidioc_s_tuner			= cx88sdr_s_tuner,
	.vidioc_enum_freq_bands		= cx88sdr_enum_freq_bands,
	.vidioc_g_frequency		= cx88sdr_g_frequency,
	.vidioc_s_frequency		= cx88sdr_s_frequency,
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

const struct video_device cx88sdr_template = {
	.device_caps	= (V4L2_CAP_SDR_CAPTURE | V4L2_CAP_TUNER |
			   V4L2_CAP_READWRITE),
	.fops		= &cx88sdr_fops,
	.ioctl_ops	= &cx88sdr_ioctl_ops,
	.name		= CX88SDR_V4L2_NAME,
	.release	= video_device_release_empty,
};

static void cx88sdr_gain_set(struct cx88sdr_dev *dev)
{
	ctrl_iowrite32(dev, MO_AGC_GAIN_ADJ4, (1 << 23) | (dev->gain << 16) |
					      (0xff << 8));
}

void cx88sdr_agc_setup(struct cx88sdr_dev *dev)
{
	ctrl_iowrite32(dev, MO_AGC_BACK_VBI, (1 << 25) | (0x100 << 16) | 0xfff);
	ctrl_iowrite32(dev, MO_AGC_SYNC_SLICER, 0x0);
	ctrl_iowrite32(dev, MO_AGC_SYNC_TIP2, (0x20 << 17) | 0xf);
	ctrl_iowrite32(dev, MO_AGC_SYNC_TIP3, (0x1e48 << 16) | (0xff << 8) | 0x8);
	ctrl_iowrite32(dev, MO_AGC_GAIN_ADJ2, (0x20 << 17) | 0xf);
	ctrl_iowrite32(dev, MO_AGC_GAIN_ADJ3, (0x28 << 16) | (0x28 << 8) | 0x50);
	cx88sdr_gain_set(dev);
}

void cx88sdr_input_set(struct cx88sdr_dev *dev)
{
	ctrl_iowrite32(dev, MO_INPUT_FORMAT, (1 << 16) | (dev->input << 14) |
					     (1 << 13) | (1 << 4) | 0x1);
}

int cx88sdr_adc_fmt_set(struct cx88sdr_dev *dev)
{
	switch (dev->pixelformat) {
	case V4L2_SDR_FMT_RU8:
		ctrl_iowrite32(dev, MO_CAPTURE_CTRL, (1 << 6) | (3 << 1));
		break;
	case V4L2_SDR_FMT_RU16LE:
		ctrl_iowrite32(dev, MO_CAPTURE_CTRL, (1 << 6) | (1 << 5) | (3 << 1));
		break;
	default:
		return -EINVAL;
	}

	switch (dev->sdr_band) {
	case CX88SDR_BAND_00:
		ctrl_iowrite32(dev, MO_SCONV_REG, (1 << 17) * 2); // Freq / 2
		ctrl_iowrite32(dev, MO_PLL_REG, (1 << 26) | (0x14 << 20)); // Freq / 5 / 8 * 20
		break;
	case CX88SDR_BAND_01:
		ctrl_iowrite32(dev, MO_SCONV_REG, (1 << 17)); // Freq
		ctrl_iowrite32(dev, MO_PLL_REG, (0x10 << 20)); // Freq / 2 / 8 * 16
		break;
	case CX88SDR_BAND_02:
		ctrl_iowrite32(dev, MO_SCONV_REG, (1 << 17) * 4 / 5); // Freq * 5 / 4
		ctrl_iowrite32(dev, MO_PLL_REG, (0x14 << 20)); // Freq / 2 / 8 * 20
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx88sdr_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cx88sdr_dev *dev = container_of(ctrl->handler,
					       struct cx88sdr_dev, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		dev->gain = ctrl->val;
		cx88sdr_gain_set(dev);
		break;
	case V4L2_CID_CX88SDR_INPUT:
		dev->input = ctrl->val;
		cx88sdr_input_set(dev);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

const struct v4l2_ctrl_ops cx88sdr_ctrl_ops = {
	.s_ctrl = cx88sdr_s_ctrl,
};

static const char * const cx88sdr_ctrl_input_menu_strings[] = {
	"Input 1",
	"Input 2",
	"Input 3",
	"Input 4",
	NULL,
};

const struct v4l2_ctrl_config cx88sdr_ctrl_input = {
	.ops	= &cx88sdr_ctrl_ops,
	.id	= V4L2_CID_CX88SDR_INPUT,
	.name	= "Input",
	.type	= V4L2_CTRL_TYPE_MENU,
	.min	= CX88SDR_INPUT_00,
	.max	= CX88SDR_INPUT_03,
	.def	= CX88SDR_INPUT_00,
	.qmenu	= cx88sdr_ctrl_input_menu_strings,
};
