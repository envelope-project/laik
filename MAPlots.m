lambdaV = linspace(0.001, 0.01, 10);
tV = linspace(1, 100, 10);

eX = zeros(length(lambdaV), length(tV));
eT = zeros(length(lambdaV), length(tV));
sT = zeros(length(lambdaV), length(tV));
for row = 1:length(lambdaV)
    for col = 1:length(tV)
        lambda = lambdaV(row);
        t = tV(col);
        eX(row, col) = (1 - exp(-lambda * t) * (lambda * t + 1))/(lambda * (1 - exp(-lambda * t)));
        eT(row, col) = (1 - exp(-lambda * t))/(lambda * exp(-lambda * t));
        sT(row, col) = eT(row, col) / t;
    end
end
gc = figure;
colormap(gray * 0.6 + 0.4);
surf(tV, lambdaV, eX);
colorbar;
axis([1 100 0.001 0.01 0 inf]);
title('Expected Time Wasted on a Failure for the Restart Strategy')
xlabel('Original runtime t_o (s)');
ylabel('Failure rate (\lambda)');
zlabel('Expected time wasted');

%pos = get(gc,'Position');

saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-x.eps','epsc')
%print(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-x.pdf','-dpdf') % then print it

gc = figure;
colormap(gray * 0.6 +0.4);
surf(tV, lambdaV, eT);
colorbar;
axis([1 100 0.001 0.01 0 inf]);
title('Expected Runtime with Failures using the Restart Strategy')
xlabel('Original runtime t_o (s)');
ylabel('Failure rate (\lambda)');
zlabel('Expected runtime t');

%pos = get(gc,'Position');
%set(gc,'PaperPositionMode','Auto','PaperUnits','Points','PaperSize',[pos(3), pos(4)])

saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-t.eps','epsc')


gc = figure;
colormap(gray * 0.6 +0.4);
surf(tV, lambdaV, sT);
colorbar;
axis([1 100 0.001 0.01 1 inf]);
title('Possible speedup from Restart Strategy to the Ideal Runtime')
xlabel('Original runtime t_o (s)');
ylabel('Failure rate (\lambda)');
zlabel('Potential for speedup');

%pos = get(gc,'Position');
%set(gc,'PaperPositionMode','Auto','PaperUnits','Points','PaperSize',[pos(3), pos(4)])

saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\speedup-t.eps','epsc')


% eNewT = zeros(length(lambdaV), length(tV));
% for row = 1:length(lambdaV)
%     for col = 1:length(tV)
%         lambda = lambdaV(row);
%         t = tV(col);
%         eNewT(row, col) = (1 - exp(-lambda * t) * (lambda * t + 1))/(lambda * (1 - exp(-lambda * t)));
%     end
% end

gc = figure;
to=60;
tr=5;
eNewT = zeros(length(lambdaV));
for row = 1:length(lambdaV)
    lambda = lambdaV(row);
    eNewT(row) = ((1 - exp(-lambda .* to)) / (lambda * exp(-lambda .* to))) + ((exp(lambda .* to) - 1) .* tr);
end

plot(lambdaV, eNewT)
title('Expected time to solution using the restart strategy')
xlabel('Failure rate (\lambda)');
ylabel('Expected time to solution (s)');
saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\restart-expectation.eps','epsc')
