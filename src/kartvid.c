/*
 * kartvid.c: primordial image processing for Mario Kart 64 analytics
 */

#include <err.h>
#include <libgen.h>
#include <stdint.h>
#include <stdlib.h>

#include <png.h>

#include "compat.h"
#include "img.h"
#include "kv.h"

static void usage(const char *);
static int cmd_and(int, char *[]);
static int cmd_compare(int, char *[]);
static int cmd_translatexy(int, char *[]);
static int cmd_ident(int, char *[]);
static int cmd_video(int, char *[]);

typedef struct {
	const char 	 *kvc_name;
	int		(*kvc_func)(int, char *[]);
	const char 	 *kvc_args;
	const char	 *kvc_usage;
} kv_cmd_t;

static kv_cmd_t kv_commands[] = {
    { "and", cmd_and, "input1 input2 output",
      "logical-and pixel values of two images" },
    { "compare", cmd_compare, "image mask",
      "compute difference score for the given image and mask" },
    { "translatexy", cmd_translatexy, "input output x-offset y-offset",
      "shift the given image using the given x and y offsets" },
    { "ident", cmd_ident, "image",
      "report the current game state for the given image" },
    { "video", cmd_video, "input1 ...",
      "emit race events for an entire video" }
};

static int kv_ncommands = sizeof (kv_commands) / sizeof (kv_commands[0]);
static const char *kv_arg0;

int kv_debug = 0;

int
main(int argc, char *argv[])
{
	int i, status;
	kv_cmd_t *kcp = NULL;

	kv_arg0 = argv[0];

	if (argc < 2)
		usage("too few arguments");

	for (i = 0; i < kv_ncommands; i++) {
		kcp = &kv_commands[i];

		if (strcmp(argv[1], kcp->kvc_name) == 0)
			break;
	}

	if (i == kv_ncommands)
		usage("unknown command");

	status = kcp->kvc_func(argc - 2, argv + 2);

	if (status == EXIT_USAGE)
		usage("missing arguments");

	return (status);
}

static void
usage(const char *message)
{
	int i;
	const char *name;
	kv_cmd_t *kcp;

	name = basename((char *)kv_arg0);
	warnx("too few arguments");

	for (i = 0; i < kv_ncommands; i++) {
		kcp = &kv_commands[i];
		(void) fprintf(stderr, "\n    %s %s %s\n", name,
		    kcp->kvc_name, kcp->kvc_args);
		(void) fprintf(stderr, "        %s\n", kcp->kvc_usage);
	}

	exit(EXIT_USAGE);
}

static int
cmd_compare(int argc, char *argv[])
{
	img_t *image, *mask;
	int rv;

	if (argc < 2)
		return (EXIT_USAGE);

	image = img_read(argv[0]);
	mask = img_read(argv[1]);

	if (mask == NULL || image == NULL) {
		img_free(image);
		return (EXIT_FAILURE);
	}

	if (image->img_width != mask->img_width ||
	    image->img_height != mask->img_height) {
		warnx("image dimensions do not match");
		rv = EXIT_FAILURE;
	} else {
		(void) printf("%f\n", img_compare(image, mask));
		rv = EXIT_SUCCESS;
	}

	img_free(image);
	img_free(mask);
	return (rv);
}

static int
cmd_and(int argc, char *argv[])
{
	img_t *image, *mask;
	FILE *outfp;
	int rv;

	if (argc < 3)
		return (EXIT_USAGE);

	image = img_read(argv[0]);
	mask = img_read(argv[1]);

	if (mask == NULL || image == NULL) {
		img_free(image);
		return (EXIT_FAILURE);
	}

	if (image->img_width != mask->img_width ||
	    image->img_height != mask->img_height) {
		warnx("image dimensions do not match");
		img_free(image);
		img_free(mask);
		return (EXIT_FAILURE);
	}

	if ((outfp = fopen(argv[2], "w")) == NULL) {
		warn("fopen %", argv[1]);
		img_free(image);
		img_free(mask);
		return (EXIT_FAILURE);
	}

	img_and(image, mask);
	rv = img_write_ppm(image, outfp);
	img_free(image);
	img_free(mask);
	return (rv);
}

static int
cmd_translatexy(int argc, char *argv[])
{
	img_t *image, *newimage;
	char *q;
	FILE *outfp;
	int rv;
	long dx, dy;

	if (argc < 4)
		return (EXIT_USAGE);

	image = img_read(argv[0]);
	if (image == NULL)
		return (EXIT_FAILURE);

	outfp = fopen(argv[1], "w");
	if (outfp == NULL) {
		warn("fopen %s", argv[1]);
		img_free(image);
		return (EXIT_FAILURE);
	}

	dx = strtol(argv[2], &q, 0);
	if (*q != '\0')
		warnx("x offset value truncated to %d", dx);

	dy = strtol(argv[3], &q, 0);
	if (*q != '\0')
		warnx("y offset value truncated to %d", dy);

	newimage = img_translatexy(image, dx, dy);
	if (newimage == NULL) {
		warn("failed to translate image");
		img_free(image);
		return (EXIT_FAILURE);
	}

	rv = img_write_ppm(newimage, outfp);
	img_free(newimage);
	return (rv);
}

static int
cmd_ident(int argc, char *argv[])
{
	img_t *image;
	kv_screen_t info;

	if (argc < 1)
		return (EXIT_USAGE);

	if (kv_init(dirname((char *)kv_arg0)) != 0) {
		warnx("failed to initialize masks");
		return (EXIT_FAILURE);
	}

	image = img_read(argv[0]);
	if (image == NULL) {
		warnx("failed to read %s", argv[0]);
		return (EXIT_FAILURE);
	}

	if (kv_ident(image, &info, B_TRUE) != 0) {
		warnx("failed to process image");
	} else {
		kv_screen_print(&info, stdout);
	}

	return (EXIT_SUCCESS);
}

static int
cmd_video(int argc, char *argv[])
{
	int i;
	img_t *image;
	kv_screen_t ks, pks;
	kv_screen_t *ksp, *pksp;

	int last_start = -1;

	if (kv_init(dirname((char *)kv_arg0)) != 0) {
		warnx("failed to initialize masks");
		return (EXIT_USAGE);
	}

	ksp = &ks;
	pksp = &pks;

	for (i = 0; i < argc; i++) {
		/*
		 * Ignore the first KV_MIN_RACE_FRAMES after a starting frame.
		 * This avoids catching what may look like multiple "start"
		 * frames right next to each other, and also avoids pointless
		 * changes in player position in the first few seconds of the
		 * race.
		 */
		if (last_start != -1 && i - last_start < KV_MIN_RACE_FRAMES)
			continue;

		image = img_read(argv[i]);

		if (image == NULL) {
			warnx("failed to read %s", argv[i]);
			continue;
		}

		kv_ident(image, ksp, B_FALSE);
		img_free(image);

		if (ksp->ks_events & KVE_RACE_START) {
			kv_ident(image, ksp, B_TRUE);
			last_start = i;
			*pksp = *ksp;
			(void) printf("%s (time %dm:%02ds): ", argv[i],
			    (int)(i / KV_FRAMERATE) / 60,
			    (int)(i / KV_FRAMERATE) % 60);
			kv_screen_print(ksp, stdout);
			continue;
		}

		/* Ignore frames up to the first race start. */
		if (last_start == -1)
			continue;

		if (kv_screen_invalid(ksp, pksp))
			continue;

		if (kv_screen_compare(ksp, pksp) == 0)
			continue;

		(void) printf("%s (time %dm:%02ds): ", argv[i],
		    i / 30 / 60, (i / 30) % 60);
		kv_screen_print(ksp, stdout);
		*pksp = *ksp;
	}

	return (EXIT_SUCCESS);
}
