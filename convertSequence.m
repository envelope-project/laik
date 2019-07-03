for i = 6:80
imc = imread(sprintf('output/data_c1_%i_0.ppm', i - 1));
imwrite(imc, sprintf('output/data_c1_%i_0.png', i - 1), 'png');
end

for i = 1:80
imc = imread(sprintf('output/data_dW_%i_0.ppm', i - 1));
imwrite(imc, sprintf('output/data_dW_%i_0.png', i - 1), 'png');
end
