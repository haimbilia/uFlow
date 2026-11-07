// Calibrated to exposed frame regions in the supplied menu reference.
const float xn=(x-96.f)/96.f,yn=(y-96.f)/96.f;
const float lower=std::clamp((y-147.f)/2.f,0.f,1.f)*std::clamp((177.f-y)/2.f,0.f,1.f);
const float side=std::clamp((std::abs(x-96.f)-(83.f-(y-147.f)))/1.3f+.5f,0.f,1.f);
const float basis[]={1.f,xn,yn,xn*yn,xn*xn,yn*yn,expf(-powf((depth-1.f)/1.7f,2.f))*(1.f),expf(-powf((depth-1.f)/1.7f,2.f))*(xn),expf(-powf((depth-1.f)/1.7f,2.f))*(yn),expf(-powf((depth-4.f)/2.6f,2.f))*(1.f),expf(-powf((depth-4.f)/2.6f,2.f))*(xn),expf(-powf((depth-4.f)/2.6f,2.f))*(yn),expf(-powf((depth-8.f)/3.8f,2.f))*(1.f),expf(-powf((depth-8.f)/3.8f,2.f))*(xn),expf(-powf((depth-8.f)/3.8f,2.f))*(yn),expf(-powf((depth-14.f)/5.f,2.f))*(1.f),expf(-powf((depth-14.f)/5.f,2.f))*(xn),expf(-powf((depth-14.f)/5.f,2.f))*(yn),expf(-powf((depth-23.f)/8.f,2.f))*(1.f),expf(-powf((depth-23.f)/8.f,2.f))*(xn),expf(-powf((depth-23.f)/8.f,2.f))*(yn),expf(-powf((x-20.f)/14.f,2.f)-powf((y-20.f)/14.f,2.f)),expf(-powf((x-172.f)/20.f,2.f)-powf((y-172.f)/20.f,2.f)),expf(-powf((x-20.f)/16.f,2.f)-powf((y-172.f)/16.f,2.f)),expf(-powf((x-172.f)/16.f,2.f)-powf((y-20.f)/16.f,2.f)),expf(-powf((x-96.f)/70.f,2.f)-powf((y-176.f)/11.f,2.f)),lower,lower*side,lower*yn};
static constexpr float coefficients[][3]={
{39.2023307f,160.1775917f,181.2885954f},
{-6.4473057f,-10.0547957f,-10.9013333f},
{-3.6064934f,14.7186745f,12.8909035f},
{-2.0864332f,-5.1622168f,-5.6253951f},
{3.8634095f,8.4477611f,9.3982285f},
{3.0220089f,8.1110017f,9.2304609f},
{5.9276056f,-0.8894617f,-1.7363854f},
{10.7157805f,12.1346404f,11.2234678f},
{-7.0296729f,-23.0103307f,-23.6092503f},
{13.5762159f,14.4155142f,11.2939948f},
{2.5864040f,3.9450632f,4.0717912f},
{0.5646966f,-14.0067039f,-12.8706167f},
{13.2364486f,16.6423585f,13.2839008f},
{3.1366001f,4.6184685f,4.3579996f},
{6.6318597f,-3.4606097f,-4.7099890f},
{12.6599332f,14.8742339f,10.9437679f},
{0.4583762f,0.4274474f,1.0670641f},
{13.6947325f,9.9917757f,8.6728586f},
{3.6309911f,9.7200578f,15.0418047f},
{-2.2593261f,-4.3794272f,-2.9909187f},
{-6.3804092f,-11.3070137f,-22.1348261f},
{7.6698281f,9.3008629f,6.0221118f},
{4.4205946f,6.4489238f,6.3659713f},
{-2.7820019f,-8.6446243f,-5.3462272f},
{4.8333983f,2.8870163f,1.8697075f},
{-6.3645971f,-24.9155552f,-22.1042465f},
{4.6250532f,11.2695304f,15.8299808f},
{-2.3585882f,-4.3240782f,-4.1467033f},
{-3.3245311f,-13.0655904f,-20.2879305f},
};
r=g=b=0;
for(unsigned i=0;i<sizeof(basis)/sizeof(basis[0]);++i){r+=basis[i]*coefficients[i][0];g+=basis[i]*coefficients[i][1];b+=basis[i]*coefficients[i][2];}
a=255*coverage;