# mostly same tests as in tests/, but using 1 and 4 procsses with TCP2 backend

# local test config
-include ../../Makefile.config

export LAIK_BACKEND=tcp2
export LAUNCHER=$(SDIR)./tcp2run

TDIR=$(SDIR)./../common

TESTS= \
    test-vsum test-vsum2 \
    test-spmv test-spmv2 test-spmv2r \
    test-spmv2-shrink test-spmv2-shrink-inc \
    test-jac1d test-jac1d-repart \
    test-jac2d test-jac2d-gen test-jac2d-noc \
    test-jac3d test-jac3d-gen test-jac3dr test-jac3d-noc test-jac3dr-noc \
    test-jac3de test-jac3der test-jac3da test-jac3dar \
    test-jac3dri test-jac3deri test-jac3dari test-jac3d-rgx3 \
    test-markov test-markov2 test-markov2f \
    test-propagation2d test-propagation2do \
    test-kvstest test-location test-spaces \
    test-resize test-vsum3 test-jac1d-resize

.PHONY: $(TESTS)

all: clean $(TESTS)

test-vsum:
	$(TDIR)/test-vsum-1.sh
	$(TDIR)/test-vsum-4.sh

test-vsum2:
	$(TDIR)/test-vsum2-1.sh
	$(TDIR)/test-vsum2-4.sh

test-spmv:
	$(TDIR)/test-spmv-1.sh
	$(TDIR)/test-spmv-4.sh

test-spmv2:
	$(TDIR)/test-spmv2-1.sh
	$(TDIR)/test-spmv2-4.sh

test-spmv2r:
	$(TDIR)/test-spmv2r-1.sh
	$(TDIR)/test-spmv2r-4.sh

test-spmv2-shrink:
	$(TDIR)/test-spmv2-shrink-4.sh

test-spmv2-shrink-inc:
	$(TDIR)/test-spmv2-shrink-inc-4.sh

test-jac1d:
	$(TDIR)/test-jac1d-1.sh
	$(TDIR)/test-jac1d-4.sh

test-jac1d-repart:
	$(TDIR)/test-jac1d-repart-1.sh
	$(TDIR)/test-jac1d-repart-4.sh

test-jac2d:
	$(TDIR)/test-jac2d-1.sh
	$(TDIR)/test-jac2d-4.sh

test-jac2d-gen:
	$(TDIR)/test-jac2d-gen-4.sh

test-jac2d-noc:
	$(TDIR)/test-jac2d-noc-4.sh

test-jac3d:
	$(TDIR)/test-jac3d-1.sh
	$(TDIR)/test-jac3d-4.sh

test-jac3d-gen:
	$(TDIR)/test-jac3d-gen-4.sh

test-jac3dr:
	$(TDIR)/test-jac3dr-1.sh
	$(TDIR)/test-jac3dr-4.sh

test-jac3dri:
	$(TDIR)/test-jac3dri-4.sh

test-jac3d-rgx3:
	$(TDIR)/test-jac3d-rgx3-4.sh

test-jac3de:
	$(TDIR)/test-jac3de-1.sh
	$(TDIR)/test-jac3de-4.sh

test-jac3der:
	$(TDIR)/test-jac3der-1.sh
	$(TDIR)/test-jac3der-4.sh

test-jac3deri:
	$(TDIR)/test-jac3deri-4.sh

test-jac3da:
	$(TDIR)/test-jac3da-1.sh
	$(TDIR)/test-jac3da-4.sh

test-jac3dar:
	$(TDIR)/test-jac3dar-1.sh
	$(TDIR)/test-jac3dar-4.sh

test-jac3dari:
	$(TDIR)/test-jac3dari-4.sh

test-jac3d-noc:
	$(TDIR)/test-jac3d-noc-4.sh

test-jac3dr-noc:
	$(TDIR)/test-jac3dr-noc-4.sh

test-markov:
	$(TDIR)/test-markov-1.sh
	$(TDIR)/test-markov-4.sh

test-markov2:
	$(TDIR)/test-markov2-1.sh
	$(TDIR)/test-markov2-4.sh

test-markov2f:
	$(TDIR)/test-markov2f-1.sh
	$(TDIR)/test-markov2f-4.sh

test-propagation2d:
	$(TDIR)/test-propagation2d-1.sh
	$(TDIR)/test-propagation2d-4.sh

test-propagation2do:
	$(TDIR)/test-propagation2do-4.sh

test-kvstest:
	$(TDIR)/test-kvstest-1.sh
	$(TDIR)/test-kvstest-4.sh

test-location:
	$(TDIR)/test-location-4.sh

test-spaces:
	$(TDIR)/test-spaces-4.sh

test-resize:
	$(SDIR)./test-resize-2-2.sh
	$(SDIR)./test-resize-3-r1.sh
	$(SDIR)./test-resize-2-s1-r1.sh
	$(SDIR)./test-resize-3-r12.sh

test-vsum3:
	$(SDIR)./test-vsum3-2-2.sh
	$(SDIR)./test-vsum3-3-r1.sh
	$(SDIR)./test-vsum3-2-s1-r1.sh
	$(SDIR)./test-vsum3-3-r12.sh

test-jac1d-resize:
	$(SDIR)./test-jac1d-resize-2-2.sh
	$(SDIR)./test-jac1d-resize-4-r12.sh

clean:
	rm -rf *.out

