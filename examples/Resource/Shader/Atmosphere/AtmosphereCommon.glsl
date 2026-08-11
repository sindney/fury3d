// AtmosphereCommon.glsl - shared math for the atmosphere LUT passes, the sky
// draw and the cloud layer. Fragment-shader port of the UE SkyAtmosphere
// reference (sebh/UnrealEngineSkyAtmosphere, EGSR 2020, MIT) to GLSL 330.
// Units inside: km. Engine world is cm; convert at the boundary (CM_TO_KM).
// No #version here - included after the version line by the includer.

const float ATM_PI = 3.14159265359;
const float CM_TO_KM = 0.00001;

uniform float u_bottom_radius;   // planet radius, km
uniform float u_top_radius;      // top of atmosphere, km
uniform vec3  u_rayleigh_scat;   // per-km scattering
uniform float u_rayleigh_density_exp_scale;  // -1/H, H ~ 8 km
uniform float u_mie_scat;
uniform float u_mie_ext;         // scattering + absorption
uniform float u_mie_density_exp_scale;       // -1/H, H ~ 1.2 km
uniform float u_mie_g;
uniform vec3  u_ozone_ext;       // absorption only
uniform float u_ozone_center_km; // density tent center altitude
uniform float u_ozone_width_km;  // density tent half width
uniform vec3  u_ground_albedo;
uniform vec3  u_sun_dir;         // world, toward the sun
uniform vec3  u_sun_color;
uniform float u_sun_intensity;
uniform float u_view_height;     // camera altitude above surface, km

float atm_density_rayleigh(float h) { return exp(u_rayleigh_density_exp_scale * h); }
float atm_density_mie(float h)      { return exp(u_mie_density_exp_scale * h); }
float atm_density_ozone(float h)    { return max(0.0, 1.0 - abs(h - u_ozone_center_km) / u_ozone_width_km); }

// scattering + extinction coefficients at altitude h (km above surface)
void atm_medium(float h, out vec3 scat, out vec3 ext)
{
	float dr = atm_density_rayleigh(h);
	float dm = atm_density_mie(h);
	float dob = atm_density_ozone(h);
	scat = u_rayleigh_scat * dr + vec3(u_mie_scat * dm);
	ext = u_rayleigh_scat * dr + vec3(u_mie_ext * dm) + u_ozone_ext * dob;
}

float atm_phase_rayleigh(float c) { return (3.0 / (16.0 * ATM_PI)) * (1.0 + c * c); }

float atm_phase_mie(float c)
{
	float g = u_mie_g;
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * ATM_PI * pow(max(1e-4, 1.0 + g2 - 2.0 * g * c), 1.5));
}

// distance to exit the top sphere; -1 when the ray never does (below ground)
float atm_ray_exit_top(vec3 p, vec3 d)
{
	float b = dot(p, d);
	float c = dot(p, p) - u_top_radius * u_top_radius;
	float h = b * b - c;
	if (h < 0.0) return -1.0;
	return -b + sqrt(h);
}

// distance to hit the ground sphere; -1 when missed
float atm_ray_ground(vec3 p, vec3 d)
{
	float b = dot(p, d);
	float c = dot(p, p) - u_bottom_radius * u_bottom_radius;
	float h = b * b - c;
	if (h < 0.0) return -1.0;
	float t = -b - sqrt(h);
	return t > 0.0 ? t : -1.0;
}

// --- transmittance LUT mapping (Bruneton chord parameterization) ---------

vec2 atm_transmittance_uv(float r, float mu)
{
	float H = sqrt(u_top_radius * u_top_radius - u_bottom_radius * u_bottom_radius);
	float rho = sqrt(max(0.0, r * r - u_bottom_radius * u_bottom_radius));
	float d = -r * mu + sqrt(max(0.0, r * r * (mu * mu - 1.0) + u_top_radius * u_top_radius));
	float dMin = u_top_radius - r;
	float dMax = rho + H;
	return vec2((d - dMin) / (dMax - dMin), rho / H);
}

void atm_transmittance_inv_uv(vec2 uv, out float r, out float mu)
{
	float H = sqrt(u_top_radius * u_top_radius - u_bottom_radius * u_bottom_radius);
	float rho = uv.y * H;
	r = sqrt(rho * rho + u_bottom_radius * u_bottom_radius);
	float dMin = u_top_radius - r;
	float dMax = rho + H;
	float d = mix(dMin, dMax, uv.x);
	mu = d <= 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}

vec3 atm_sun_transmittance(sampler2D lut, float r, float mu)
{
	return texture(lut, atm_transmittance_uv(r, mu)).rgb;
}

// --- sky-view LUT mapping -------------------------------------------------
// x: sun-relative azimuth folded to [0, pi] (symmetric). y: zenith with a
// quadratic warp concentrating texels near the horizon (0 = nadir,
// 0.5 = horizon, 1 = zenith).

float atm_horizon_mu(float r)
{
	return -sqrt(max(0.0, r * r - u_bottom_radius * u_bottom_radius)) / r;
}

vec2 atm_skyview_uv(float mu, float cosAzim)
{
	float r = u_bottom_radius + u_view_height;
	float muH = atm_horizon_mu(r);
	float v;
	if (mu >= muH)
	{
		float t = clamp((mu - muH) / (1.0 - muH), 0.0, 1.0);
		v = 0.5 + 0.5 * t * t;
	}
	else
	{
		float t = clamp((mu + 1.0) / (muH + 1.0), 0.0, 1.0);
		v = 0.5 * t * t;
	}
	return vec2(acos(clamp(cosAzim, -1.0, 1.0)) / ATM_PI, v);
}

void atm_skyview_inv_uv(vec2 uv, out float mu, out float cosAzim)
{
	cosAzim = cos(uv.x * ATM_PI);
	float r = u_bottom_radius + u_view_height;
	float muH = atm_horizon_mu(r);
	if (uv.y >= 0.5)
	{
		float t = sqrt((uv.y - 0.5) * 2.0);
		mu = muH + t * (1.0 - muH);
	}
	else
	{
		float t = sqrt(uv.y * 2.0);
		mu = -1.0 + t * (muH + 1.0);
	}
}

// --- shared atmosphere march ----------------------------------------------
// Marches from p (planet-relative, km) along d for tMax km, accumulating
// single scattering from the sun plus the multi-scatter LUT's isotropic
// field. Returns inscatter; outTrans gets the path transmittance.

vec3 atm_march(sampler2D transLut, sampler2D msLut, vec3 p, vec3 d, float tMax, int samples, out vec3 outTrans)
{
	float dt = tMax / float(samples);
	float cosViewSun = dot(d, u_sun_dir);
	float phaseR = atm_phase_rayleigh(cosViewSun);
	float phaseM = atm_phase_mie(cosViewSun);
	vec3 L = vec3(0.0);
	vec3 T = vec3(1.0);
	for (int i = 0; i < samples; i++)
	{
		vec3 q = p + d * (dt * (float(i) + 0.5));
		float r = max(length(q), u_bottom_radius);
		float h = r - u_bottom_radius;
		vec3 scat, ext;
		atm_medium(h, scat, ext);
		vec3 stepT = exp(-ext * dt);
		float sunMu = dot(normalize(q), u_sun_dir);
		vec3 sunT = atm_sun_transmittance(transLut, r, sunMu);
		vec3 ms = texture(msLut, vec2(sunMu * 0.5 + 0.5,
			clamp(h / (u_top_radius - u_bottom_radius), 0.0, 1.0))).rgb;
		vec3 S = sunT * (u_rayleigh_scat * atm_density_rayleigh(h) * phaseR
			+ vec3(u_mie_scat * atm_density_mie(h) * phaseM))
			+ ms * scat;
		// midpoint rule with per-step transmittance
		L += T * S * dt;
		T *= stepT;
	}
	outTrans = T;
	return L * u_sun_color * u_sun_intensity;
}
