im1 = imread('output/data_d1_0_0.ppm');
width = size(im1,1);
height = size(im1,2);
ims = zeros(width, height, 3, 1);

for i = 1:996
ims(1:width, 1:height, :, i) = imread(sprintf('output/data_d1_%i_0.ppm', i - 1));
end

ims = ims .* (1/255);
implay(ims, 3);
