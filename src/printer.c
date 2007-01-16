/*
  Hatari - printer.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Printer communication. When bytes are sent from the ST they are sent to these
  functions via 'Printer_TransferByteTo()'. This will then open a file and
  direct the output to this. These bytes are buffered up (to improve speed) and
  this also allow us to detect when the stream goes into idle - at which point
  we close the file/printer.
  NOTE - Tab's are converted to spaces as the PC 'Tab' setting differs to that
  of the ST.
*/
const char Printer_rcsid[] = "Hatari $Id: printer.c,v 1.19 2007-01-16 18:42:59 thothy Exp $";

#include "main.h"
#include "dialog.h"
#include "file.h"
#include "printer.h"
#include "screen.h"

/* #define PRINTER_DEBUG */

#define PRINTER_FILENAME "/hatari.prn"

#define PRINTER_TAB_SETTING  8          /* A 'Tab' on the ST is 8 spaces */
#define PRINTER_IDLE_CLOSE   (4*50)     /* After 4 seconds, close printer */

#define PRINTER_BUFFER_SIZE  2048       /* 2k buffer which when full will be written to printer/file */

static unsigned char PrinterBuffer[PRINTER_BUFFER_SIZE];   /* Buffer to store character before output */
static size_t nPrinterBufferChars;      /* # characters in above buffer */
static int nPrinterBufferCharsOnLine;
static BOOL bConnectedPrinter=FALSE;
static BOOL bPrinterFile = FALSE;
static int nIdleCount;

static FILE *PrinterFileHandle;


/*-----------------------------------------------------------------------*/
/**
 * Initialise Printer
 */
void Printer_Init(void)
{
#ifdef PRINTER_DEBUG
	fprintf(stderr,"Printer_Init()\n");
#endif

	/* A valid file name for printing is already set up in configuration.c.
	 * But we check it again since the user might have entered an invalid
	 * file name in the ~/.hatari.cfg file... */
	if (strlen(ConfigureParams.Printer.szPrintToFileName) <= 1)
	{
		/* construct filename for printing.... */
		if (getenv("HOME") != NULL
				&& strlen(getenv("HOME"))+strlen(PRINTER_FILENAME) < sizeof(ConfigureParams.Printer.szPrintToFileName))
			sprintf(ConfigureParams.Printer.szPrintToFileName, "%s%s", getenv("HOME"), PRINTER_FILENAME);
		else
			sprintf(ConfigureParams.Printer.szPrintToFileName, ".%s",PRINTER_FILENAME);
	}

#ifdef PRINTER_DEBUG
	fprintf(stderr,"Filename for printing: %s \n", ConfigureParams.Printer.szPrintToFileName);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Uninitialise Printer
 */
void Printer_UnInit(void)
{
	/* Close any open files */
	Printer_CloseAllConnections();

#ifdef PRINTER_DEBUG
	fprintf(stderr,"Printer_UnInit()\n");
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Close all open files etc.
 */
void Printer_CloseAllConnections(void)
{
	/* Empty buffer */
	Printer_EmptyInternalBuffer();

	/* Close any open files */
	Printer_CloseFile();

	/* Signal finished with printing */
	bConnectedPrinter = FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Open file on disk, to which all printer output will be sent.
 */
BOOL Printer_OpenFile(void)
{

	bPrinterFile = TRUE;

	/* open printer file... */
	PrinterFileHandle = fopen(ConfigureParams.Printer.szPrintToFileName, "a+");
	if (!PrinterFileHandle)
		bPrinterFile = FALSE;

	return bPrinterFile;
}


/*-----------------------------------------------------------------------*/
/**
 * Close file on disk, if we have one open.
 */
void Printer_CloseFile(void)
{
	/* Do have file open? */
	if (bPrinterFile)
	{
		/* Close */
		fclose(PrinterFileHandle);
		bPrinterFile = FALSE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Empty to file on disk.
 */
void Printer_EmptyFile(void)
{
	/* Do have file open? */
	if (bPrinterFile)
	{
		/* Write bytes out */
		if (fwrite((unsigned char *)PrinterBuffer,sizeof(unsigned char),nPrinterBufferChars,PrinterFileHandle) < nPrinterBufferChars)
		{
			/* we wrote less then all chars in the buffer --> ERROR */
			fprintf(stderr,"Printer_EmptyFile(): ERROR not all chars were written\n");
		}
		/* Reset */
		Printer_ResetInternalBuffer();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Reset Printer Buffer
 */
void Printer_ResetInternalBuffer(void)
{
	nPrinterBufferChars = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset character line
 */
void Printer_ResetCharsOnLine(void)
{
	nPrinterBufferCharsOnLine = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Empty Printer Buffer
 */
BOOL Printer_EmptyInternalBuffer(void)
{
	/* Write bytes to file */
	if (nPrinterBufferChars>0)
	{
		if (bPrinterFile)
			Printer_EmptyFile();

		return TRUE;
	}

	/* Nothing to do */
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Return TRUE if byte is standard ASCII character which is OK to output
 */
BOOL Printer_ValidByte(unsigned char Byte)
{
	/* Return/New line? */
	if ((Byte == 0x0d) || (Byte == 0x0a))
		return TRUE;
	/* Normal character? */
	if ((Byte >= 32) && (Byte < 127))
		return TRUE;
	/* Tab */
	if (Byte == '\t')
		return TRUE;
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Add byte to our internal buffer, and when full write out - needed to speed
 */
void Printer_AddByteToInternalBuffer(unsigned char Byte)
{
	/* Is buffer full? If so empty */
	if (nPrinterBufferChars == PRINTER_BUFFER_SIZE)
		Printer_EmptyInternalBuffer();
	/* Add character */
	PrinterBuffer[nPrinterBufferChars++] = Byte;
	/* Add count of character on line */
	if (!((Byte == 0xd) || (Byte == 0xa)))
		nPrinterBufferCharsOnLine++;
}


/*-----------------------------------------------------------------------*/
/**
 * Add 'Tab' to internal buffer
 */
void Printer_AddTabToInternalBuffer(void)
{
	int i,NumSpaces;

	/* Is buffer full? If so empty */
	if (nPrinterBufferChars >= (PRINTER_BUFFER_SIZE-PRINTER_TAB_SETTING))
		Printer_EmptyInternalBuffer();
	/* Add tab - convert to 'PRINTER_TAB_SETTING' space */
	NumSpaces = PRINTER_TAB_SETTING-(nPrinterBufferCharsOnLine%PRINTER_TAB_SETTING);
	for(i = 0; i < NumSpaces; i++)
	{
		PrinterBuffer[nPrinterBufferChars++] = ' ';
		nPrinterBufferCharsOnLine++;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Pass byte from emulator to printer
 */
BOOL Printer_TransferByteTo(unsigned char Byte)
{
	/* Do we want to output to a printer/file? */
	if (!ConfigureParams.Printer.bEnablePrinting)
		return FALSE;   /* Failed if printing disabled */

	/* Have we made a connection to our printer/file? */
	if (!bConnectedPrinter)
	{
		bConnectedPrinter = Printer_OpenFile();

		/* Reset the printer */
		Printer_ResetInternalBuffer();
		Printer_ResetCharsOnLine();
	}

	/* Is all OK? */
	if (bConnectedPrinter)
	{
		/* Add byte to our buffer, if is useable character */
		if (Printer_ValidByte(Byte))
		{
			if (Byte == '\t')
				Printer_AddTabToInternalBuffer();
			else
				Printer_AddByteToInternalBuffer(Byte);
			if (Byte == 0xd)
				nPrinterBufferCharsOnLine = 0;
		}

		return TRUE;    /* OK */
	}
	else
		return FALSE;   /* Failed */
}


/*-----------------------------------------------------------------------*/
/**
 * Empty printer buffer, and if remains idle for set time close connection
 * (ie close file, stop printer)
 */
void Printer_CheckIdleStatus(void)
{
	/* Is anything waiting for printer? */
	if (Printer_EmptyInternalBuffer())
	{
		nIdleCount = 0;
	}
	else
	{
		nIdleCount++;
		/* Has printer been idle? */
		if (nIdleCount >= PRINTER_IDLE_CLOSE)
		{
			/* Close printer output */
			Printer_CloseAllConnections();
			nIdleCount = 0;
		}
	}
}
