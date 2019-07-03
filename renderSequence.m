im1 = imread('data_0_0.pgm');
width = size(im1,1);
height = size(im1,2);
ims = zeros(width, height, 1);

for i = 1:75
ims(1:width, 1:height, i) = imread(sprintf('data_%i_0.pgm', i - 1));
end

ims = ims .* (1/255);
implay(ims, 3);
