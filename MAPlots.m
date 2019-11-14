lambdaV = linspace(0.001, 0.03, 10);
tV = linspace(1, 100, 10);

eX = zeros(length(lambdaV), length(tV));
eT = zeros(length(lambdaV), length(tV));
eTC = zeros(length(lambdaV), length(tV));
sT = zeros(length(lambdaV), length(tV));

tr = 6.5;
fc = 0.1;
tc = 0.08;
for row = 1:length(lambdaV)
    for col = 1:length(tV)
        lambda = lambdaV(row);
        t = tV(col);
        eX(row, col) = (1 - exp(-lambda * t) * (lambda * t + 1))/(lambda * (1 - exp(-lambda * t)));
        eT(row, col) = (1 - exp(-lambda * t))/(lambda * exp(-lambda * t));
        sT(row, col) = eT(row, col) / t;
        eTC(row, col) = t * fc * (exp(lambda * ((1/fc) + tc))-1) * ((1/lambda)+tr);
    end
end
gc = figure;
colormap(gray * 0.6 + 0.4);
surf(tV, lambdaV, eX);
colorbar;
axis([min(tV) max(tV) min(lambdaV) max(lambdaV) 0 inf]);
title('Expected Time Wasted on a Failure for the Restart Strategy')
xlabel('Original Runtime t_o (s)');
ylabel('Failure Rate (\lambda)');
zlabel('Expected Time Wasted');

%pos = get(gc,'Position');

saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-x.eps','epsc')
%print(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-x.pdf','-dpdf') % then print it

gc = figure;
colormap(gray * 0.6 +0.4);
surf(tV, lambdaV, eT);
colorbar;
axis([min(tV) max(tV) min(lambdaV) max(lambdaV) 0 inf]);
title('Expected Runtime with Failures using the Restart Strategy')
xlabel('Original Runtime t_o (s)');
ylabel('Failure Rate (\lambda)');
zlabel('Expected Runtime t');

%pos = get(gc,'Position');
%set(gc,'PaperPositionMode','Auto','PaperUnits','Points','PaperSize',[pos(3), pos(4)])

saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-t.eps','epsc')


gc = figure;
colormap(gray * 0.6 +0.4);
surf(tV, lambdaV, sT);
colorbar;
axis([min(tV) max(tV) min(lambdaV) max(lambdaV) 1 inf]);
title('Possible Speedup from Restart Strategy to the Ideal Runtime')
xlabel('Original Runtime t_o (s)');
ylabel('Failure Rate (\lambda)');
zlabel('Potential for Speedup');

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
title('Expected Time to Solution using the Restart Strategy')
xlabel('Failure Rate (\lambda)');
ylabel('Expected Time to Solution (s)');
saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\restart-expectation.eps','epsc')


gc = figure;
colormap(gray * 0.6 +0.4);
surf(tV, lambdaV, eTC);
colorbar;
axis([min(tV) max(tV) min(lambdaV) max(lambdaV) 0 inf]);
title('Expected Runtime with Failures using the Checkpoint Strategy')
xlabel('Original Runtime t_o (s)');
ylabel('Failure Rate (\lambda)');
zlabel('Expected Runtime t');

%pos = get(gc,'Position');
%set(gc,'PaperPositionMode','Auto','PaperUnits','Points','PaperSize',[pos(3), pos(4)])

saveas(gc,'C:\Users\vincent_bode\Desktop\VTStuff\GitSync\TUM\MA\res\expected-t-c.eps','epsc')

