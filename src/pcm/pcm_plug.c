/*
 *  PCM - Plug
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include "pcm_local.h"
#include "pcm_plugin.h"

typedef struct {
	snd_pcm_t *req_slave;
	int close_slave;
	snd_pcm_t *slave;
	snd_pcm_route_ttable_entry_t *ttable;
	unsigned int tt_ssize, tt_cused, tt_sused;
} snd_pcm_plug_t;

static int snd_pcm_plug_close(snd_pcm_t *pcm)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	int err, result = 0;
	if (plug->ttable)
		free(plug->ttable);
	assert(plug->slave == plug->req_slave);
	if (plug->close_slave) {
		err = snd_pcm_close(plug->req_slave);
		if (err < 0)
			result = err;
	}
	free(plug);
	return result;
}

static int snd_pcm_plug_nonblock(snd_pcm_t *pcm, int nonblock)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	return snd_pcm_nonblock(plug->slave, nonblock);
}

static int snd_pcm_plug_async(snd_pcm_t *pcm, int sig, pid_t pid)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	return snd_pcm_async(plug->slave, sig, pid);
}

static int snd_pcm_plug_info(snd_pcm_t *pcm, snd_pcm_info_t *info)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	snd_pcm_t *slave = plug->req_slave;
	int err;
	
	if ((err = snd_pcm_info(slave, info)) < 0)
		return err;
	return 0;
}

static snd_pcm_format_t linear_preferred_formats[] = {
#ifdef SND_LITTLE_ENDIAN
	SND_PCM_FORMAT_S16_LE,
	SND_PCM_FORMAT_U16_LE,
	SND_PCM_FORMAT_S16_BE,
	SND_PCM_FORMAT_U16_BE,
#else
	SND_PCM_FORMAT_S16_BE,
	SND_PCM_FORMAT_U16_BE,
	SND_PCM_FORMAT_S16_LE,
	SND_PCM_FORMAT_U16_LE,
#endif
#ifdef SND_LITTLE_ENDIAN
	SND_PCM_FORMAT_S24_LE,
	SND_PCM_FORMAT_U24_LE,
	SND_PCM_FORMAT_S24_BE,
	SND_PCM_FORMAT_U24_BE,
#else
	SND_PCM_FORMAT_S24_BE,
	SND_PCM_FORMAT_U24_BE,
	SND_PCM_FORMAT_S24_LE,
	SND_PCM_FORMAT_U24_LE,
#endif
#ifdef SND_LITTLE_ENDIAN
	SND_PCM_FORMAT_S32_LE,
	SND_PCM_FORMAT_U32_LE,
	SND_PCM_FORMAT_S32_BE,
	SND_PCM_FORMAT_U32_BE,
#else
	SND_PCM_FORMAT_S32_BE,
	SND_PCM_FORMAT_U32_BE,
	SND_PCM_FORMAT_S32_LE,
	SND_PCM_FORMAT_U32_LE,
#endif
	SND_PCM_FORMAT_S8,
	SND_PCM_FORMAT_U8
};

static snd_pcm_format_t nonlinear_preferred_formats[] = {
	SND_PCM_FORMAT_MU_LAW,
	SND_PCM_FORMAT_A_LAW,
	SND_PCM_FORMAT_IMA_ADPCM,
};

static snd_pcm_format_t snd_pcm_plug_slave_format(snd_pcm_format_t format, const snd_pcm_format_mask_t *format_mask)
{
	int w, u, e, wid, w1, dw;
	snd_pcm_format_mask_t lin = { SND_PCM_FMTBIT_LINEAR };
	if (snd_pcm_format_mask_test(format_mask, format))
		return format;
	if (!snd_pcm_format_mask_test(&lin, format)) {
		unsigned int i;
		switch (snd_enum_to_int(format)) {
		case SND_PCM_FORMAT_MU_LAW:
		case SND_PCM_FORMAT_A_LAW:
		case SND_PCM_FORMAT_IMA_ADPCM:
			for (i = 0; i < sizeof(linear_preferred_formats) / sizeof(linear_preferred_formats[0]); ++i) {
				snd_pcm_format_t f = linear_preferred_formats[i];
				if (snd_pcm_format_mask_test(format_mask, f))
					return f;
			}
			/* Fall through */
		default:
			return SND_PCM_FORMAT_UNKNOWN;
		}

	}
	snd_mask_intersect(&lin, format_mask);
	if (snd_mask_empty(&lin)) {
		unsigned int i;
		for (i = 0; i < sizeof(nonlinear_preferred_formats) / sizeof(nonlinear_preferred_formats[0]); ++i) {
			snd_pcm_format_t f = nonlinear_preferred_formats[i];
			if (snd_pcm_format_mask_test(format_mask, f))
				return f;
		}
		return SND_PCM_FORMAT_UNKNOWN;
	}
	w = snd_pcm_format_width(format);
	u = snd_pcm_format_unsigned(format);
	e = snd_pcm_format_big_endian(format);
	w1 = w;
	dw = 8;
	for (wid = 0; wid < 4; ++wid) {
		int end, e1 = e;
		for (end = 0; end < 2; ++end) {
			int sgn, u1 = u;
			for (sgn = 0; sgn < 2; ++sgn) {
				snd_pcm_format_t f;
				f = snd_pcm_build_linear_format(w1, u1, e1);
				assert(f != SND_PCM_FORMAT_UNKNOWN);
				if (snd_pcm_format_mask_test(format_mask, f))
					return f;
				u1 = !u1;
			}
			e1 = !e1;
		}
		if (w1 < 32)
			w1 += dw;
		else {
			w1 = w - 8;
			dw = -8;
		}
	}
	return SND_PCM_FORMAT_UNKNOWN;
}

#define SND_PCM_FMTBIT_PLUG (SND_PCM_FMTBIT_LINEAR | \
			     (1 << snd_enum_to_int(SND_PCM_FORMAT_MU_LAW)) | \
			     (1 << snd_enum_to_int(SND_PCM_FORMAT_A_LAW)) | \
			     (1 << snd_enum_to_int(SND_PCM_FORMAT_IMA_ADPCM)))


static void snd_pcm_plug_clear(snd_pcm_t *pcm)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	snd_pcm_t *slave = plug->req_slave;
	/* Clear old plugins */
	if (plug->slave != slave) {
		snd_pcm_close(plug->slave);
		plug->slave = slave;
		pcm->fast_ops = slave->fast_ops;
		pcm->fast_op_arg = slave->fast_op_arg;
	}
}

typedef struct {
	snd_pcm_access_t access;
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
} snd_pcm_plug_params_t;

static int snd_pcm_plug_change_rate(snd_pcm_t *pcm, snd_pcm_t **new, snd_pcm_plug_params_t *clt, snd_pcm_plug_params_t *slv)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	int err;
	assert(snd_pcm_format_linear(slv->format));
	if (clt->rate == slv->rate)
		return 0;
	err = snd_pcm_rate_open(new, NULL, slv->format, slv->rate, plug->slave, plug->slave != plug->req_slave);
	if (err < 0)
		return err;
	slv->access = clt->access;
	slv->rate = clt->rate;
	if (snd_pcm_format_linear(clt->format))
		slv->format = clt->format;
	return 1;
}

static int snd_pcm_plug_change_channels(snd_pcm_t *pcm, snd_pcm_t **new, snd_pcm_plug_params_t *clt, snd_pcm_plug_params_t *slv)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	unsigned int tt_ssize, tt_cused, tt_sused;
	snd_pcm_route_ttable_entry_t *ttable;
	int err;
	assert(snd_pcm_format_linear(slv->format));
	if (clt->channels == slv->channels)
		return 0;
	if (clt->rate != slv->rate &&
	    clt->channels > slv->channels)
		return 0;
	     
	ttable = plug->ttable;
	if (ttable) {
		tt_ssize = plug->tt_ssize;
		tt_cused = plug->tt_cused;
		tt_sused = plug->tt_sused;
	} else {
		unsigned int k;
		unsigned int c = 0, s = 0;
		int n;
		tt_ssize = slv->channels;
		tt_cused = clt->channels;
		tt_sused = slv->channels;
		ttable = alloca(tt_cused * tt_sused * sizeof(*ttable));
		for (k = 0; k < tt_cused * tt_sused; ++k)
			ttable[k] = 0;
		if (clt->channels > slv->channels) {
			n = clt->channels;
		} else {
			n = slv->channels;
		}
		while (n-- > 0) {
			snd_pcm_route_ttable_entry_t v = FULL;
			if (pcm->stream == SND_PCM_STREAM_PLAYBACK &&
			    clt->channels > slv->channels) {
				int srcs = clt->channels / slv->channels;
				if (s < clt->channels % slv->channels)
					srcs++;
				v /= srcs;
			} else if (pcm->stream == SND_PCM_STREAM_CAPTURE &&
				   slv->channels > clt->channels) {
				int srcs = slv->channels / clt->channels;
				if (s < slv->channels % clt->channels)
					srcs++;
				v /= srcs;
			}
			ttable[c * tt_ssize + s] = v;
			if (++c == clt->channels)
				c = 0;
			if (++s == slv->channels)
				s = 0;
		}
	}
	err = snd_pcm_route_open(new, NULL, slv->format, (int) slv->channels, ttable, tt_ssize, tt_cused, tt_sused, plug->slave, plug->slave != plug->req_slave);
	if (err < 0)
		return err;
	slv->channels = clt->channels;
	slv->access = clt->access;
	if (snd_pcm_format_linear(clt->format))
		slv->format = clt->format;
	return 1;
}

static int snd_pcm_plug_change_format(snd_pcm_t *pcm, snd_pcm_t **new, snd_pcm_plug_params_t *clt, snd_pcm_plug_params_t *slv)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	int err;
	snd_pcm_format_t cfmt;
	int (*f)(snd_pcm_t **_pcm, const char *name, snd_pcm_format_t sformat, snd_pcm_t *slave, int close_slave);
	if (snd_pcm_format_linear(slv->format)) {
		/* Conversion is done in another plugin */
		if (clt->format == slv->format ||
		    clt->rate != slv->rate ||
		    clt->channels != slv->channels)
			return 0;
		cfmt = clt->format;
		switch (snd_enum_to_int(clt->format)) {
		case SND_PCM_FORMAT_MU_LAW:
			f = snd_pcm_mulaw_open;
			break;
		case SND_PCM_FORMAT_A_LAW:
			f = snd_pcm_alaw_open;
			break;
		case SND_PCM_FORMAT_IMA_ADPCM:
			f = snd_pcm_adpcm_open;
			break;
		default:
			assert(snd_pcm_format_linear(clt->format));
			f = snd_pcm_linear_open;
			break;
		}
	} else {
		/* No conversion is needed */
		if (clt->format == slv->format &&
		    clt->rate == slv->rate &&
		    clt->channels == clt->channels)
			return 0;
		switch (snd_enum_to_int(slv->format)) {
		case SND_PCM_FORMAT_MU_LAW:
			f = snd_pcm_mulaw_open;
			break;
		case SND_PCM_FORMAT_A_LAW:
			f = snd_pcm_alaw_open;
			break;
		case SND_PCM_FORMAT_IMA_ADPCM:
			f = snd_pcm_adpcm_open;
			break;
		default:
			assert(0);
			return -EINVAL;
		}
		if (snd_pcm_format_linear(clt->format))
			cfmt = clt->format;
		else
			cfmt = SND_PCM_FORMAT_S16;
	}
	err = f(new, NULL, slv->format, plug->slave, plug->slave != plug->req_slave);
	if (err < 0)
		return err;
	slv->format = cfmt;
	slv->access = clt->access;
	return 1;
}

static int snd_pcm_plug_change_access(snd_pcm_t *pcm, snd_pcm_t **new, snd_pcm_plug_params_t *clt, snd_pcm_plug_params_t *slv)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	int err;
	if (clt->access == slv->access)
		return 0;
	err = snd_pcm_copy_open(new, NULL, plug->slave, plug->slave != plug->req_slave);
	if (err < 0)
		return err;
	slv->access = clt->access;
	return 1;
}

static int snd_pcm_plug_insert_plugins(snd_pcm_t *pcm,
				       snd_pcm_plug_params_t *client,
				       snd_pcm_plug_params_t *slave)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	int (*funcs[])(snd_pcm_t *_pcm, snd_pcm_t **new, snd_pcm_plug_params_t *s, snd_pcm_plug_params_t *d) = {
		snd_pcm_plug_change_format,
		snd_pcm_plug_change_channels,
		snd_pcm_plug_change_rate,
		snd_pcm_plug_change_channels,
		snd_pcm_plug_change_format,
		snd_pcm_plug_change_access
	};
	snd_pcm_plug_params_t p = *slave;
	unsigned int k = 0;
	while (client->format != p.format ||
	       client->channels != p.channels ||
	       client->rate != p.rate ||
	       client->access != p.access) {
		snd_pcm_t *new;
		int err;
		assert(k < sizeof(funcs)/sizeof(*funcs));
		err = funcs[k](pcm, &new, client, &p);
		if (err < 0) {
			snd_pcm_plug_clear(pcm);
			return err;
		}
		if (err) {
			plug->slave = new;
			pcm->fast_ops = new->fast_ops;
			pcm->fast_op_arg = new->fast_op_arg;
		}
		k++;
	}
	return 0;
}

static int snd_pcm_plug_hw_refine_cprepare(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params ATTRIBUTE_UNUSED)
{
	return 0;
}

static int snd_pcm_plug_hw_refine_sprepare(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *sparams)
{
	_snd_pcm_hw_params_any(sparams);
	return 0;
}

static int snd_pcm_plug_hw_refine_schange(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
					  snd_pcm_hw_params_t *sparams)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	snd_pcm_t *slave = plug->req_slave;
	unsigned int links = (SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	const snd_pcm_format_mask_t *format_mask, *sformat_mask;
	snd_pcm_format_mask_t sfmt_mask;
	int err;
	snd_pcm_format_t format;
	snd_interval_t t, buffer_size;
	const snd_interval_t *srate, *crate;
	snd_pcm_hw_param_refine_near(slave, sparams, SND_PCM_HW_PARAM_RATE,
				     params);
	snd_pcm_hw_param_refine_near(slave, sparams, SND_PCM_HW_PARAM_CHANNELS,
				     params);
	format_mask = snd_pcm_hw_param_get_mask(params,
						   SND_PCM_HW_PARAM_FORMAT);
	sformat_mask = snd_pcm_hw_param_get_mask(sparams,
						    SND_PCM_HW_PARAM_FORMAT);
	snd_mask_none(&sfmt_mask);
	for (format = 0; format <= SND_PCM_FORMAT_LAST; snd_enum_incr(format)) {
		snd_pcm_format_t f;
		if (!snd_pcm_format_mask_test(format_mask, format))
			continue;
		if (snd_pcm_format_mask_test(sformat_mask, format))
			f = format;
		else {
			f = snd_pcm_plug_slave_format(format, sformat_mask);
			if (f == SND_PCM_FORMAT_UNKNOWN)
				continue;
		}
		snd_pcm_format_mask_set(&sfmt_mask, f);
	}

	err = snd_pcm_hw_param_set_mask(slave, sparams, SND_CHANGE,
					SND_PCM_HW_PARAM_FORMAT, &sfmt_mask);
	assert(err >= 0);

	if (snd_pcm_hw_param_never_eq(params, SND_PCM_HW_PARAM_FORMAT, sparams) ||
	    snd_pcm_hw_param_never_eq(params, SND_PCM_HW_PARAM_CHANNELS, sparams) ||
	    snd_pcm_hw_param_never_eq(params, SND_PCM_HW_PARAM_RATE, sparams) ||
	    snd_pcm_hw_param_never_eq(params, SND_PCM_HW_PARAM_ACCESS, sparams)) {
		snd_pcm_access_mask_t access_mask = { SND_PCM_ACCBIT_MMAP };
		_snd_pcm_hw_param_set_mask(sparams, SND_PCM_HW_PARAM_ACCESS,
					   &access_mask);
	}
	if (snd_pcm_hw_param_always_eq(params, SND_PCM_HW_PARAM_RATE, sparams))
		links |= (SND_PCM_HW_PARBIT_PERIOD_SIZE |
			  SND_PCM_HW_PARBIT_BUFFER_SIZE);
	else {
		snd_interval_copy(&buffer_size, snd_pcm_hw_param_get_interval(params, SND_PCM_HW_PARAM_BUFFER_SIZE));
		snd_interval_unfloor(&buffer_size);
		crate = snd_pcm_hw_param_get_interval(params, SND_PCM_HW_PARAM_RATE);
		srate = snd_pcm_hw_param_get_interval(sparams, SND_PCM_HW_PARAM_RATE);
		snd_interval_muldiv(&buffer_size, srate, crate, &t);
		err = _snd_pcm_hw_param_set_interval(sparams, SND_PCM_HW_PARAM_BUFFER_SIZE, &t);
		if (err < 0)
			return err;
	}
	err = _snd_pcm_hw_params_refine(sparams, links, params);
	if (err < 0)
		return err;
	return 0;
}
	
static int snd_pcm_plug_hw_refine_cchange(snd_pcm_t *pcm ATTRIBUTE_UNUSED,
					  snd_pcm_hw_params_t *params,
					  snd_pcm_hw_params_t *sparams)
{
	unsigned int links = (SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	const snd_pcm_format_mask_t *format_mask, *sformat_mask;
	snd_pcm_format_mask_t fmt_mask;
	int err;
	snd_pcm_format_t format;
	snd_interval_t t;
	const snd_interval_t *sbuffer_size;
	const snd_interval_t *srate, *crate;
	unsigned int rate_min, srate_min;
	int rate_mindir, srate_mindir;
	format_mask = snd_pcm_hw_param_get_mask(params,
						SND_PCM_HW_PARAM_FORMAT);
	sformat_mask = snd_pcm_hw_param_get_mask(sparams,
						 SND_PCM_HW_PARAM_FORMAT);
	snd_mask_none(&fmt_mask);
	for (format = 0; format <= SND_PCM_FORMAT_LAST; snd_enum_incr(format)) {
		snd_pcm_format_t f;
		if (!snd_pcm_format_mask_test(format_mask, format))
			continue;
		if (snd_pcm_format_mask_test(sformat_mask, format))
			f = format;
		else {
			f = snd_pcm_plug_slave_format(format, sformat_mask);
			if (f == SND_PCM_FORMAT_UNKNOWN)
				continue;
		}
		snd_pcm_format_mask_set(&fmt_mask, format);
	}

	err = _snd_pcm_hw_param_set_mask(params, 
					 SND_PCM_HW_PARAM_FORMAT, &fmt_mask);
	if (err < 0)
		return err;

	/* This is a temporary hack, waiting for a better solution */
	rate_min = snd_pcm_hw_param_get_min(params, SND_PCM_HW_PARAM_RATE, &rate_mindir);
	srate_min = snd_pcm_hw_param_get_min(sparams, SND_PCM_HW_PARAM_RATE, &srate_mindir);
	if (rate_min == srate_min && srate_mindir > rate_mindir) {
		err = _snd_pcm_hw_param_set_min(params, SND_PCM_HW_PARAM_RATE, srate_min, srate_mindir);
		if (err < 0)
			return err;
	}

	if (snd_pcm_hw_param_always_eq(params, SND_PCM_HW_PARAM_RATE, sparams))
		links |= (SND_PCM_HW_PARBIT_PERIOD_SIZE |
			  SND_PCM_HW_PARBIT_BUFFER_SIZE);
	else {
		sbuffer_size = snd_pcm_hw_param_get_interval(sparams, SND_PCM_HW_PARAM_BUFFER_SIZE);
		crate = snd_pcm_hw_param_get_interval(params, SND_PCM_HW_PARAM_RATE);
		srate = snd_pcm_hw_param_get_interval(sparams, SND_PCM_HW_PARAM_RATE);
		snd_interval_muldiv(sbuffer_size, crate, srate, &t);
		snd_interval_floor(&t);
		err = _snd_pcm_hw_param_set_interval(params, SND_PCM_HW_PARAM_BUFFER_SIZE, &t);
		if (err < 0)
			return err;
	}
	err = _snd_pcm_hw_params_refine(params, links, sparams);
	if (err < 0)
		return err;
	/* FIXME */
	params->info &= ~(SND_PCM_INFO_MMAP | SND_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_pcm_plug_hw_refine_slave(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	return snd_pcm_hw_refine(plug->req_slave, params);
}

static int snd_pcm_plug_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return snd_pcm_hw_refine_slave(pcm, params,
				       snd_pcm_plug_hw_refine_cprepare,
				       snd_pcm_plug_hw_refine_cchange,
				       snd_pcm_plug_hw_refine_sprepare,
				       snd_pcm_plug_hw_refine_schange,
				       snd_pcm_plug_hw_refine_slave);
}

static int snd_pcm_plug_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	snd_pcm_t *slave = plug->req_slave;
	snd_pcm_plug_params_t clt_params, slv_params;
	snd_pcm_hw_params_t sparams;
	int err;

	err = snd_pcm_plug_hw_refine_sprepare(pcm, &sparams);
	assert(err >= 0);
	err = snd_pcm_plug_hw_refine_schange(pcm, params, &sparams);
	assert(err >= 0);
	err = snd_pcm_hw_refine_soft(slave, &sparams);
	assert(err >= 0);

	clt_params.access = snd_pcm_hw_params_get_access(params);
	clt_params.format = snd_pcm_hw_params_get_format(params);
	clt_params.channels = snd_pcm_hw_params_get_channels(params);
	clt_params.rate = snd_pcm_hw_params_get_rate(params, 0);

	slv_params.format = snd_pcm_hw_params_get_format(&sparams);
	slv_params.channels = snd_pcm_hw_params_get_channels(&sparams);
	slv_params.rate = snd_pcm_hw_params_get_rate(&sparams, 0);
	snd_pcm_plug_clear(pcm);
	if (!(clt_params.format == slv_params.format &&
	      clt_params.channels == slv_params.channels &&
	      clt_params.rate == slv_params.rate &&
	      snd_pcm_hw_params_test_access(slave, &sparams,
					    clt_params.access) >= 0)) {
		slv_params.access = snd_pcm_hw_params_set_access_first(slave, &sparams);
		err = snd_pcm_plug_insert_plugins(pcm, &clt_params, &slv_params);
		if (err < 0)
			return err;
	}
	slave = plug->slave;
	err = _snd_pcm_hw_params(slave, params);
	if (err < 0) {
		snd_pcm_plug_clear(pcm);
		return err;
	}
	pcm->hw_ptr = slave->hw_ptr;
	pcm->appl_ptr = slave->appl_ptr;
	return 0;
}

static int snd_pcm_plug_hw_free(snd_pcm_t *pcm)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	snd_pcm_t *slave = plug->slave;
	int err = snd_pcm_hw_free(slave);
	snd_pcm_plug_clear(pcm);
	return err;
}

static int snd_pcm_plug_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t * params)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	return snd_pcm_sw_params(plug->slave, params);
}

static int snd_pcm_plug_channel_info(snd_pcm_t *pcm, snd_pcm_channel_info_t *info)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	return snd_pcm_channel_info(plug->slave, info);
}

static int snd_pcm_plug_mmap(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	return 0;
}

static int snd_pcm_plug_munmap(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	return 0;
}

static void snd_pcm_plug_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_plug_t *plug = pcm->private_data;
	snd_output_printf(out, "Plug PCM: ");
	snd_pcm_dump(plug->slave, out);
}

snd_pcm_ops_t snd_pcm_plug_ops = {
	close: snd_pcm_plug_close,
	info: snd_pcm_plug_info,
	hw_refine: snd_pcm_plug_hw_refine,
	hw_params: snd_pcm_plug_hw_params,
	hw_free: snd_pcm_plug_hw_free,
	sw_params: snd_pcm_plug_sw_params,
	channel_info: snd_pcm_plug_channel_info,
	dump: snd_pcm_plug_dump,
	nonblock: snd_pcm_plug_nonblock,
	async: snd_pcm_plug_async,
	mmap: snd_pcm_plug_mmap,
	munmap: snd_pcm_plug_munmap,
};

int snd_pcm_plug_open(snd_pcm_t **pcmp,
		      const char *name,
		      snd_pcm_route_ttable_entry_t *ttable,
		      unsigned int tt_ssize,
		      unsigned int tt_cused, unsigned int tt_sused,
		      snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_plug_t *plug;
	assert(pcmp && slave);
	plug = calloc(1, sizeof(snd_pcm_plug_t));
	if (!plug)
		return -ENOMEM;
	plug->slave = plug->req_slave = slave;
	plug->close_slave = close_slave;
	plug->ttable = ttable;
	plug->tt_ssize = tt_ssize;
	plug->tt_cused = tt_cused;
	plug->tt_sused = tt_sused;
	
	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		free(plug);
		return -ENOMEM;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_PLUG;
	pcm->stream = slave->stream;
	pcm->mode = slave->mode;
	pcm->ops = &snd_pcm_plug_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = slave->fast_ops;
	pcm->fast_op_arg = slave->fast_op_arg;
	pcm->private_data = plug;
	pcm->poll_fd = slave->poll_fd;
	pcm->hw_ptr = slave->hw_ptr;
	pcm->appl_ptr = slave->appl_ptr;
	*pcmp = pcm;

	return 0;
}

int snd_pcm_plug_open_hw(snd_pcm_t **pcmp, const char *name, int card, int device, int subdevice, snd_pcm_stream_t stream, int mode)
{
	snd_pcm_t *slave;
	int err;
	err = snd_pcm_hw_open(&slave, NULL, card, device, subdevice, stream, mode);
	if (err < 0)
		return err;
	return snd_pcm_plug_open(pcmp, name, 0, 0, 0, 0, slave, 1);
}

#define MAX_CHANNELS 32

int _snd_pcm_plug_open(snd_pcm_t **pcmp, const char *name,
		       snd_config_t *root, snd_config_t *conf, 
		       snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	int err;
	snd_pcm_t *spcm;
	snd_config_t *slave = NULL, *sconf;
	snd_config_t *tt = NULL;
	snd_pcm_route_ttable_entry_t *ttable = NULL;
	unsigned int cused, sused;
	const char *args;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (snd_pcm_conf_generic_id(id))
			continue;
		if (strcmp(id, "slave") == 0) {
			slave = n;
			continue;
		}
		if (strcmp(id, "ttable") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			tt = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("slave is not defined");
		return -EINVAL;
	}
	err = snd_pcm_slave_conf(root, slave, &sconf, &args, 0);
	if (err < 0)
		return err;
	if (tt) {
		ttable = malloc(MAX_CHANNELS * MAX_CHANNELS * sizeof(*ttable));
		err = snd_pcm_route_load_ttable(tt, ttable, MAX_CHANNELS, MAX_CHANNELS,
						&cused, &sused, -1);
		if (err < 0)
			return err;
	}
		
	err = snd_pcm_open_slave(&spcm, root, sconf, args, stream, mode);
	if (err < 0)
		return err;
	err = snd_pcm_plug_open(pcmp, name, ttable, MAX_CHANNELS, cused, sused, spcm, 1);
	if (err < 0)
		snd_pcm_close(spcm);
	return err;
}
				
