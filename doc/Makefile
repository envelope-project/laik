# Create documentation from Markdown files via pandoc.
# For doxygen documentation, just run 'doxygen'
#
# Supported formats:
#
# make html
#   make a set of HTML files
#
# make onehtml
#   create one big HTML file: doc.html
#
# make pdf
#   create doc.pdf

SRCS = index.md Debugging.md TODO.md

html: $(SRCS:.md=.html)

onehtml: doc.html

%.html: %.md
	sed 's/.md/.html/' $< | pandoc -o $@

pdf: $(SRCS)
	sed 's/(.*\.md#/(#/' $(SRCS) | pandoc -o doc.html

doc.html: $(SRCS)
    # assume all links use anchors
	sed 's/(.*\.md#/(#/' $(SRCS) | pandoc -o doc.html

clean:
	rm -f *.html *.pdf
