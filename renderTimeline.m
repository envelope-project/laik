
output = readtable('out/out_stat.txt');
output = sortrows(output, [6, 8]);

figure();
scatter(output.Var8, output.Var6 + 1);
label(output.Var4, output.Var8, output.Var6 + 1, output.Var6);
title('Timeline');

figure();
hold on;
legend();
for rank = 0 : numel(unique(output.Var6)) -1
    plot(output.Var8(output.Var6 == rank, :), output.Var10(output.Var6 == rank, :), 'DisplayName', sprintf("Rank %i", rank));
    label(output.Var4(output.Var6 == rank, :), output.Var8(output.Var6 == rank, :), output.Var10(output.Var6 == rank, :), output.Var6(output.Var6 == rank, :));
end
title('MEM Usage');

figure();
hold on;
legend();
for rank = 0 : numel(unique(output.Var6)) -1
    plot(output.Var8(output.Var6 == rank, :), output.Var13(output.Var6 == rank, :), 'DisplayName', sprintf("Rank %i", rank));
    label(output.Var4(output.Var6 == rank, :), output.Var8(output.Var6 == rank, :), output.Var13(output.Var6 == rank, :), output.Var6(output.Var6 == rank, :));
end
title('NET Usage');

function label(labels, xVals, yVals, ranks)
    displs = linspace(0, 0, numel(xVals));
    %// Assign labels.
    for labelID = 1 : numel(xVals)
        if(labelID > 2 && abs(xVals(labelID) - xVals(labelID - 1)) > 0.1)
            displs(ranks(labelID) + 1) = 0.05;
        else
            displs(ranks(labelID) + 1) = displs(ranks(labelID) + 1) + 0.05;
        end
       text(xVals(labelID), yVals(labelID) - displs(ranks(labelID) + 1), labels(labelID), 'HorizontalAlignment', 'left');
       line([xVals(labelID), xVals(labelID)], [yVals(labelID) - displs(ranks(labelID) + 1), yVals(labelID)]);
    end
end
