// Calibrated to exposed frame regions in the supplied menu reference.
const float xn=(x-96.f)/96.f,yn=(y-96.f)/96.f;
const float lower=std::clamp((y-147.f)/2.f,0.f,1.f)*std::clamp((177.f-y)/2.f,0.f,1.f);
const float side=std::clamp((std::abs(x-96.f)-(83.f-(y-147.f)))/1.3f+.5f,0.f,1.f);
const float sheen=std::clamp((112.f-22.f*xn*xn-y)/2.f,0.f,1.f);
const float basis[]={1.f,xn,yn,xn*yn,xn*xn,yn*yn,expf(-powf((depth-0.7f)/0.65f,2.f))*(1.f),expf(-powf((depth-0.7f)/0.65f,2.f))*(xn),expf(-powf((depth-0.7f)/0.65f,2.f))*(yn),expf(-powf((depth-1.f)/1.7f,2.f))*(1.f),expf(-powf((depth-1.f)/1.7f,2.f))*(xn),expf(-powf((depth-1.f)/1.7f,2.f))*(yn),expf(-powf((depth-4.f)/2.6f,2.f))*(1.f),expf(-powf((depth-4.f)/2.6f,2.f))*(xn),expf(-powf((depth-4.f)/2.6f,2.f))*(yn),expf(-powf((depth-8.f)/3.8f,2.f))*(1.f),expf(-powf((depth-8.f)/3.8f,2.f))*(xn),expf(-powf((depth-8.f)/3.8f,2.f))*(yn),expf(-powf((depth-14.f)/5.f,2.f))*(1.f),expf(-powf((depth-14.f)/5.f,2.f))*(xn),expf(-powf((depth-14.f)/5.f,2.f))*(yn),expf(-powf((depth-23.f)/8.f,2.f))*(1.f),expf(-powf((depth-23.f)/8.f,2.f))*(xn),expf(-powf((depth-23.f)/8.f,2.f))*(yn),expf(-powf((x-20.f)/14.f,2.f)-powf((y-20.f)/14.f,2.f)),expf(-powf((x-172.f)/20.f,2.f)-powf((y-172.f)/20.f,2.f)),expf(-powf((x-20.f)/16.f,2.f)-powf((y-172.f)/16.f,2.f)),expf(-powf((x-172.f)/16.f,2.f)-powf((y-20.f)/16.f,2.f)),expf(-powf((x-96.f)/70.f,2.f)-powf((y-176.f)/11.f,2.f)),expf(-powf((x-9.f)/6.f,2.f)-powf((y-27.f)/14.f,2.f)),expf(-powf((x-183.f)/7.f,2.f)-powf((y-165.f)/16.f,2.f)),sheen,sheen*xn,sheen*yn};
static constexpr float coefficients[][3]={
{159.2438327f,181.3250439f,172.0716009f},
{4.3654034f,5.1784543f,5.9653735f},
{0.9975298f,1.2104909f,-2.1498354f},
{-17.2222368f,-19.0314746f,-17.2970473f},
{7.9440237f,11.9852282f,11.8363222f},
{14.0743013f,11.7951308f,15.7855316f},
{-29.2633583f,-27.3431040f,-26.4582432f},
{1.4319744f,1.1459825f,0.2827457f},
{27.9053314f,26.8632514f,26.6432943f},
{6.8852876f,-6.6264853f,-5.9514101f},
{2.0165311f,1.4603189f,0.9192915f},
{-46.7182560f,-43.7067942f,-42.9768970f},
{25.2080919f,15.4189446f,15.1629883f},
{1.7327541f,1.1354262f,0.4811562f},
{5.3292916f,6.3340627f,4.9635299f},
{8.6316822f,0.6209336f,0.5685456f},
{0.8541090f,0.1855042f,-0.4585810f},
{4.8853672f,5.9532943f,5.6001857f},
{3.2335453f,-3.6749313f,-3.9425339f},
{4.1448524f,3.6969761f,3.2594418f},
{-4.7920466f,-3.5889988f,-5.5861103f},
{2.8819185f,-2.3670104f,-1.5618831f},
{1.8852067f,1.9979638f,1.3271534f},
{-13.2884347f,-13.9456225f,-14.1801161f},
{-2.3998145f,-1.2129936f,-2.2707912f},
{25.7455941f,20.1418845f,22.9625510f},
{7.4089960f,-1.4393185f,5.2868556f},
{8.8528455f,6.4357989f,6.9012864f},
{25.8383094f,22.2330396f,28.5292175f},
{61.6813537f,55.4895786f,58.9048781f},
{30.7201733f,29.8857082f,32.3345628f},
{11.2377540f,7.4829047f,12.3006712f},
{-8.2626846f,-9.0286056f,-7.5798329f},
{1.2275403f,-5.2166069f,5.7617289f},
};
r=g=b=0;
for(unsigned i=0;i<sizeof(basis)/sizeof(basis[0]);++i){r+=basis[i]*coefficients[i][0];g+=basis[i]*coefficients[i][1];b+=basis[i]*coefficients[i][2];}
const float spec=expf(-powf((x-9.f)/6.f,2.f)-powf((y-27.f)/14.f,2.f))+expf(-powf((x-183.f)/7.f,2.f)-powf((y-165.f)/16.f,2.f));
a=255*coverage*std::min(1.f,.55f+.4f*expf(-powf((depth-5.f)/6.f,2.f))+.45f*spec);