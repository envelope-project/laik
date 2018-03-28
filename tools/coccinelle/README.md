# Installing coccinelle

coccinelle is probably already packaged for your distribution, look for a
package called ```coccinelle```. Otherwise, you can of course also compile it
from source from upstream[^0].

# Writing coccinelle patches

This blog post[^1] is a good first step to understand the basic ideas behind
coccinelle. For a more advanced discussion, you might want to read the LWN
article[^2] or watch a talk from one of the authors[^3]. Furthermore, there is
an official example gallery[^4] and quite a few projects already use coccinelle,
for example systemd[^5] or the Linux kernel[^6], so you might wanna check out
how they solve a particular problem. Finally, the language is well-documented,
so have a look at the language grammar[^7]!

# Running coccinelle

You can either apply a specific patch...

    $ tools/coccinelle/coccinelle.sh /path/to/your/coccinelle.patch
    [...]
    $

... or simply apply all of LAIK's stored patches on the source tree:

    $ tools/coccinelle/coccinelle.sh
    [...]
    $

[^0]: <http://coccinelle.lip6.fr/>
[^1]: <https://home.regit.org/technical-articles/coccinelle-for-the-newbie/>
[^2]: <https://lwn.net/Articles/315686/>
[^3]: <https://www.youtube.com/watch?v=buZrNd6XkEw>
[^4]: <http://coccinellery.org/>
[^5]: <https://github.com/systemd/systemd/tree/master/coccinelle>
[^6]: <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/scripts/coccinelle>
[^7]: <http://coccinelle.lip6.fr/docs/main_grammar.html>
