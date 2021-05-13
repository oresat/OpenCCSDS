CCSDS_ROOT ?= .

CCSDS_INC :=	$(CCSDS_ROOT)/include
CCSDS_SRCDIR :=	$(CCSDS_ROOT)/src
CCSDS_SRC :=	$(CCSDS_SRCDIR)/frame_buf.c	\
				$(CCSDS_SRCDIR)/uslp.c		\
				$(CCSDS_SRCDIR)/cop.c		\
				$(CCSDS_SRCDIR)/spp.c		\
				$(CCSDS_SRCDIR)/sdls.c
