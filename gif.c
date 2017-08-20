/*
    fbv  --  simple image viewer for the linux framebuffer
    Copyright (C) 2000, 2001, 2003  Mateusz Golicz
	Modified 2017 Kyle Farnsworth (kyle@farnsworthtech.com)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#include "config.h"
#ifdef FBV_SUPPORT_GIF
#include "fbv.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <setjmp.h>
#include <gif_lib.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define min(a,b) ((a) < (b) ? (a) : (b))
#define gflush return(FH_ERROR_FILE);
#define grflush { DGifCloseFile(gft, &error); BUG return(FH_ERROR_FORMAT); }
#define mgrflush { free(lb); free(slb); DGifCloseFile(gft, &error); BUG return(FH_ERROR_FORMAT); }
#define agflush return(FH_ERROR_FORMAT);
#define agrflush { DGifCloseFile(gft, &error); BUG return(FH_ERROR_FORMAT); }

#define MAX_IMAGES 64
static int imagecount=0;  // num of images in gif file
static int imageix=0;  // current image loaded
static char *images[MAX_IMAGES];
static char *alphas[MAX_IMAGES];
static int userinputs[MAX_IMAGES];
static int disposalmethods[MAX_IMAGES];
static int delays[MAX_IMAGES];	// delay in 1/100 secs

int fh_gif_get_delay(void)
{
	return delays[imageix] * 10;
}

int fh_gif_get_disposal_method(void)
{
	return disposalmethods[imageix];
}

int fh_gif_get_userinput(void)
{
	return userinputs[imageix];
}

int fh_gif_id(char *name)
{
	int fd;
	char id[4];
	fd=open(name,O_RDONLY); if(fd==-1) return(0);
	read(fd,id,4);
	close(fd);
	if(id[0]=='G' && id[1]=='I' && id[2]=='F') return(1);
	return(0);
}

static inline void m_rend_gif_decodecolormap(unsigned char *cmb,unsigned char *rgbb,ColorMapObject *cm,int s,int l, int transparency)
{
	GifColorType *cmentry;
	int i;
	for(i=0;i<l;i++)
	{
		cmentry=&cm->Colors[cmb[i]];
		*(rgbb++)=cmentry->Red;
		*(rgbb++)=cmentry->Green;
		*(rgbb++)=cmentry->Blue;
	}
}


/* Thanks goes here to Mauro Meneghin, who implemented interlaced GIF files support */

int fh_gif_load(char *name,unsigned char **buffer, unsigned char **alpha, int x,int y)
{
	int in_nextrow[4]={8,8,4,2};   //interlaced jump to the row current+in_nextrow
	int in_beginrow[4]={0,4,2,1};  //begin pass j from that row number
	int transparency=-1;  //-1 means no transparency present
	int userinput=0, disposalmethod=0, delay=0;
	int px,py,i,ibxs;
	int j;
	char *fbptr, *wr_buffer, wr_alpha;
	char *image;
	char *lb;
	char *slb;
	GifFileType *gft;
	GifByteType *extension;
	int extcode;
	GifRecordType rt;
	ColorMapObject *cmap;
	int cmaps;
	int loadedfirstimage=0;
	int error;

	imagecount=0;
	imageix=0;
	gft=DGifOpenFileName(name, &error);
	if(gft==NULL){ fprintf(stderr, "Gif open err %d\n", error); gflush;} //////////
	do
	{
		if(DGifGetRecordType(gft,&rt) == GIF_ERROR) grflush;
		if (debugme) fprintf(stdout, "record type=%i images=%d\n", rt, imagecount);
		switch(rt)
		{
			case IMAGE_DESC_RECORD_TYPE:
				if(DGifGetImageDesc(gft)==GIF_ERROR) grflush;
				if (imagecount >= MAX_IMAGES)
					break;
				px=gft->Image.Width;
				py=gft->Image.Height;
				if (px*py > x*y)
				{
					fprintf(stderr, "bad sizes?  x=%d y=%d px=%d py=%d\n", x, y, px, py);
					break;
				}
				lb=(char*)malloc(px*3);
				slb=(char*) malloc(px);
				image=(char*)malloc(x*y*3);
				if(lb!=NULL && slb!=NULL && image!=NULL)
				{
					unsigned char *alphabuffer = NULL;
					unsigned char *alphaptr;

					images[imagecount] = image;
					userinputs[imagecount] = userinput;
					disposalmethods[imagecount] = disposalmethod;
					delays[imagecount] = delay;

					cmap=(gft->Image.ColorMap ? gft->Image.ColorMap : gft->SColorMap);
					cmaps=cmap->ColorCount;

					ibxs=ibxs*3;

					if(transparency != -1)
					{
						alphabuffer = malloc(px * py);
					}
					alphas[imagecount] = alphabuffer;
					imagecount++;

					fbptr = image;
					alphaptr = alphabuffer;
					if(!(gft->Image.Interlace))
					{
						for(i=0;i<py;i++,fbptr+=px*3)
						{
							int j;
							if(DGifGetLine(gft,(GifPixelType*)slb,px)==GIF_ERROR) mgrflush;
							m_rend_gif_decodecolormap((unsigned char*)slb,(unsigned char*)lb,cmap,cmaps,px,transparency);
							memcpy(fbptr,lb,px*3);
							if(alphaptr)
								for(j = 0; j<px; j++) *(alphaptr++) = (((unsigned char*) slb)[j] == transparency) ? 0x00 : 0xff;
						}
					}
					else
					{
						unsigned char * aptr = NULL;

						for(j=0;j<4;j++)
						{
							int k;
							if(alphaptr)
								aptr = alphaptr + (in_beginrow[j] * px);

							fbptr=image + (in_beginrow[j] * px * 3);

							for(i = in_beginrow[j]; i<py; i += in_nextrow[j], fbptr += px * 3 * in_nextrow[j], aptr += px * in_nextrow[j])
							{
								if(DGifGetLine(gft,(GifPixelType*)slb,px)==GIF_ERROR) mgrflush; /////////////
								m_rend_gif_decodecolormap((unsigned char*)slb,(unsigned char*)lb,cmap,cmaps,px,transparency);
								memcpy(fbptr,lb,px*3);
								if(alphaptr)
									for(k = 0; k<px; k++) aptr[k] = (((unsigned char*) slb)[k] == transparency) ? 0x00 : 0xff;
							}
						}
					}
					if (!loadedfirstimage)
					{
						wr_buffer = (unsigned char*)malloc(x*y*3);
						if (!wr_buffer)
							return FH_ERROR_MEM;

						memcpy(wr_buffer, image, x*y*3);
						if (alpha)
						{
							if (alphabuffer)
							{
								unsigned char *wr_alpha = malloc(x*y);
								if (wr_alpha)
								{
									memcpy(wr_alpha, alphabuffer, x*y);
									*alpha = wr_alpha;
								}
								else
								{
									free(wr_buffer);
									return FH_ERROR_MEM;
								}
							}
							else
								*alpha = NULL;
						}
						*buffer = wr_buffer;
						loadedfirstimage = 1;
					}
				}
				else
					fprintf(stderr, "No memory!\n");
				if(lb) free(lb);
				if(slb) free(slb);
				break;

			case EXTENSION_RECORD_TYPE:
				if(DGifGetExtension(gft,&extcode,&extension)==GIF_ERROR) grflush; //////////
				if (debugme && extension)
				{
					fprintf(stdout, "exten code=0x%02x : len=%d", extcode, extension[0]);
					for (i=0; i<extension[0]; i++)
						fprintf(stdout, " %02x", extension[i+1]);
					fprintf(stdout, "\n");
				}
				if(extcode==0xf9) //look image transparency in graph ctr extension
				{
					if(extension[1] & 1)
					{
						transparency = extension[4];
					}
					if(extension[1] & 2)
						userinput = 1;
					disposalmethod = (extension[1] >> 2) & 0x7;
					delay = extension[2] | (extension[3] << 8);
					if (debugme) fprintf(stdout, "delay=%d\n", delay);
				}
				while(extension!=NULL) {
					if(DGifGetExtensionNext(gft,&extension) == GIF_ERROR) grflush
					if (debugme && extension)
					{
						fprintf(stdout, "exten code=0x%02x : len=%d",
							extcode, extension[0]);
						for (i=0; i<extension[0]; i++)
							fprintf(stdout, " %02x", extension[i+1]);
						fprintf(stdout, "\n");
					}
				}
				break;
			default:
				break;
		}
	}
	while( rt!= TERMINATE_RECORD_TYPE);
	DGifCloseFile(gft, &error);
	return(FH_ERROR_OK);
}

int fh_gif_next(unsigned char **buffer, unsigned char **alpha, int x, int y)
{
	unsigned char *wr_buffer;

	if (imagecount==0)
		return(FH_ERROR_FORMAT);
	wr_buffer = (unsigned char*)malloc(x*y*3);
	if (!wr_buffer)
		return FH_ERROR_MEM;

	imageix++;
	if (imageix >= imagecount)
		imageix=0;
	memcpy(wr_buffer, images[imageix], x*y*3);
	if (alpha)
	{
		if (alphas[imageix])
		{
			unsigned char *wr_alpha = malloc(x*y);
			if (wr_alpha)
			{
				memcpy(wr_alpha, alphas[imageix], x*y);
				*alpha = wr_alpha;
			}
			else
			{
				free(wr_buffer);
				return(FH_ERROR_MEM);
			}
		}
		else
			*alpha = NULL;
	}
	*buffer = wr_buffer;
	return(FH_ERROR_OK);
}

int fh_gif_unload(void)
{
	int i;
	for (i=0; i<imagecount; i++)
	{
		if (images[i])
			free(images[i]);
		images[i] = NULL;
		if (alphas[i])
			free(alphas[i]);
		alphas[i] = NULL;
	}
	imagecount=0;
}

int fh_gif_getsize(char *name,int *x,int *y)
{
	int px,py;
	GifFileType *gft;
	GifByteType *extension;
	int extcode;
	GifRecordType rt;
	int error;

	gft=DGifOpenFileName(name, &error);
	if(gft==NULL){ fprintf(stderr, "Gif open err %d\n", error); gflush;} //////////
	do
	{
		if(DGifGetRecordType(gft,&rt) == GIF_ERROR) grflush;
		switch(rt)
		{
	    case IMAGE_DESC_RECORD_TYPE:

			if(DGifGetImageDesc(gft)==GIF_ERROR) grflush;
			px=gft->Image.Width;
			py=gft->Image.Height;
			*x=px; *y=py;
			DGifCloseFile(gft, &error);
			return(FH_ERROR_OK);
			break;
	    case EXTENSION_RECORD_TYPE:
			if(DGifGetExtension(gft,&extcode,&extension)==GIF_ERROR) grflush;
			while(extension!=NULL)
			    if(DGifGetExtensionNext(gft,&extension)==GIF_ERROR) grflush;
			break;
	    default:
			break;
		}
	}
	while( rt!= TERMINATE_RECORD_TYPE );
	DGifCloseFile(gft, &error);
	return(FH_ERROR_FORMAT);
}
#endif
