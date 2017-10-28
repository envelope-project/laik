# Create HTML/PDF from Markdown documentation via pandoc

SRCS = index.md tutorial-jacobi2d.md Debugging.md TODO.md

html: $(SRCS:.md=.html)

%.html: %.md
	sed s/\.md\)/\.html\)/ $< | pandoc -o $@

pdf: $(SRCS)
	pandoc -N -o doc.pdf -t html5 $(SRCS)

clean:
	rm -f *.html *.pdf
