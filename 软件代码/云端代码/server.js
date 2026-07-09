const express = require('express');
const mysql = require('mysql2/promise');
const cors = require('cors');
const dayjs = require('dayjs');
const JSZip = require('jszip');
const fs = require('fs');
const path = require('path');
const app = express();

// 允许所有来源访问
app.use(cors({
    origin: '*',
    methods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
    allowedHeaders: ['Content-Type', 'Authorization'],
    exposedHeaders: ['Content-Disposition']
}));

// 解析JSON请求体
app.use(express.json());

// 数据库连接池
const pool = mysql.createPool({
    host: '47.108.232.40',
    user: 'qwe123',
    password: 'XNKC47BKdppFsc3Y',
    port: 3306,
    database: 'qwe123',
    waitForConnections: true,
    connectionLimit: 30,
    queueLimit: 0,  // 0 = 无限制
    connectTimeout: 10000,  // 连接超时
    timeout: 30000,         // 查询超时
    acquireTimeout: 60000   // 从队列获取连接的超时时间
});

// 阿里云API配置
const ALIYUN_API_CONFIG = {
    apiKey: 'sk-4bbfde4680224979a1e3de1a1eb591ee',
    baseUrl: 'https://dashscope.aliyuncs.com/compatible-mode/v1'
};

// 缓存点云边界信息
const lakeBoundsCache = new Map();
const CACHE_TTL = 5 * 60 * 1000; // 5分钟缓存

// 验证码缓存
const captchaStore = new Map();
const CAPTCHA_TTL = 5 * 60 * 1000; // 5分钟过期

// 生成验证码图片
function generateCaptcha() {
    const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789';
    let code = '';
    for (let i = 0; i < 4; i++) {
        code += chars[Math.floor(Math.random() * chars.length)];
    }
    
    const width = 100;
    const height = 36;
    
    // 使用 SVG 生成验证码图片（增强干扰）
    const bgColor = '#0B1426';
    const fontColors = ['#2DD4BF', '#22D3EE', '#5EEAD4', '#67E8F9', '#A5F3FC'];
    
    let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}"><rect width="100%" height="100%" fill="${bgColor}"/>`;
    
    // 背景噪点（随机小圆点）
    for (let i = 0; i < 60; i++) {
        const cx = Math.random() * width;
        const cy = Math.random() * height;
        const r = 0.5 + Math.random() * 1.5;
        const opacity = 0.1 + Math.random() * 0.3;
        svg += `<circle cx="${cx}" cy="${cy}" r="${r}" fill="rgba(150,230,230,${opacity})"/>`;
    }
    
    // 干扰线（贝塞尔曲线）
    for (let i = 0; i < 6; i++) {
        const x1 = Math.random() * width;
        const y1 = Math.random() * height;
        const cx1 = Math.random() * width;
        const cy1 = Math.random() * height;
        const cx2 = Math.random() * width;
        const cy2 = Math.random() * height;
        const x2 = Math.random() * width;
        const y2 = Math.random() * height;
        const opacity = 0.15 + Math.random() * 0.25;
        svg += `<path d="M${x1},${y1} C${cx1},${cy1} ${cx2},${cy2} ${x2},${y2}" stroke="rgba(45,212,191,${opacity})" stroke-width="${0.5 + Math.random()}" fill="none"/>`;
    }
    
    // 波浪干扰线
    for (let i = 0; i < 3; i++) {
        let path = 'M';
        const startY = Math.random() * height;
        for (let x = 0; x <= width; x += 2) {
            const y = startY + Math.sin(x * 0.1 + i * 2) * 4;
            path += `${x},${y} `;
        }
        svg += `<path d="${path}" stroke="rgba(45,212,191,0.1)" stroke-width="1" fill="none"/>`;
    }
    
    // 添加文字（随机颜色、大小、旋转、位置偏移）
    for (let i = 0; i < code.length; i++) {
        const x = 12 + i * 22 + (Math.random() - 0.5) * 6;
        const y = 20 + (Math.random() - 0.5) * 10;
        const rotate = (Math.random() - 0.5) * 60; // 更大旋转
        const fontSize = 18 + Math.floor(Math.random() * 6);
        const color = fontColors[Math.floor(Math.random() * fontColors.length)];
        const fontStyle = Math.random() > 0.5 ? 'italic' : 'normal';
        svg += `<text x="${x}" y="${y}" fill="${color}" font-size="${fontSize}" font-family="monospace" font-weight="bold" font-style="${fontStyle}" transform="rotate(${rotate} ${x} ${y})" text-anchor="middle" dominant-baseline="middle">${code[i]}</text>`;
    }
    
    // 前景噪点
    for (let i = 0; i < 20; i++) {
        const x = Math.random() * width;
        const y = Math.random() * height;
        svg += `<rect x="${x}" y="${y}" width="1" height="1" fill="rgba(255,255,255,${0.1 + Math.random() * 0.2})"/>`;
    }
    
    svg += '</svg>';
    
    const base64 = Buffer.from(svg).toString('base64');
    return { code, image: `data:image/svg+xml;base64,${base64}` };
}

// 1. 获取湖泊概览数据
app.get('/api/lakes', async (req, res) => {
    const result = {};
    try {
        const [taskRows] = await pool.query(
            `SELECT DISTINCT task_id FROM point_cloud_data WHERE task_id IS NOT NULL AND task_id != ''`
        );
        
        const lakes = taskRows.map(row => String(row.task_id)).filter(id => id);
        
        console.log('📊 查询到的 task_id 列表:', lakes);
        
        if (lakes.length === 0) {
            return res.json({});
        }
        
        for (const lake of lakes) {
            const [sensorRows] = await pool.query(
                `SELECT depth, temperature, dissolved_oxygen, ph, conductivity, turbidity 
                 FROM sensor_data WHERE task_id = ? ORDER BY timestamp DESC LIMIT 1`,
                [lake]
            );
            
            const [pointCountRows] = await pool.query(
                `SELECT COUNT(*) AS total FROM point_cloud_data WHERE task_id = ?`,
                [lake]
            );

            const [traceCountRows] = await pool.query(
                `SELECT COUNT(*) AS total FROM robot_trace WHERE task_id = ?`,
                [lake]
            );

            const [sensorRangeRows] = await pool.query(
                `SELECT 
                    MIN(depth) as minDepth, MAX(depth) as maxDepth,
                    MIN(temperature) as minTemp, MAX(temperature) as maxTemp,
                    MIN(dissolved_oxygen) as minDO, MAX(dissolved_oxygen) as maxDO,
                    MIN(ph) as minPH, MAX(ph) as maxPH,
                    MIN(conductivity) as minCond, MAX(conductivity) as maxCond,
                    MIN(turbidity) as minTurb, MAX(turbidity) as maxTurb
                 FROM sensor_data WHERE task_id = ?`,
                [lake]
            );

            const rawRanges = sensorRangeRows[0];
            console.log(`🗄️ [${lake}] 范围:`, rawRanges);

            result[lake] = {
                sensorData: sensorRows[0] || {
                    depth: null, temperature: null, dissolved_oxygen: null,
                    ph: null, conductivity: null, turbidity: null
                },
                pointCount: pointCountRows[0]?.total || 0,
                traceCount: traceCountRows[0]?.total || 0,
                sensorRanges: {
                    minDepth: rawRanges?.minDepth ?? 45,
                    maxDepth: rawRanges?.maxDepth ?? 120,
                    minTemp: rawRanges?.minTemp ?? 0,
                    maxTemp: rawRanges?.maxTemp ?? 6,
                    minDO: rawRanges?.minDO ?? 10,
                    maxDO: rawRanges?.maxDO ?? 14,
                    minPH: rawRanges?.minPH ?? 7.5,
                    maxPH: rawRanges?.maxPH ?? 8.5,
                    minCond: rawRanges?.minCond ?? 50,
                    maxCond: rawRanges?.maxCond ?? 150,
                    minTurb: rawRanges?.minTurb ?? 0.1,
                    maxTurb: rawRanges?.maxTurb ?? 1.0
                }
            };
        }
        res.json(result);
    } catch (error) {
        console.error('❌ 获取湖泊数据失败:', error);
        res.status(500).json({ error: '获取数据失败', details: error.message });
    }
});

// ============ 获取点云边界 ============
async function getPointCloudBounds(lake) {
    const cacheKey = `bounds_${lake}`;
    const cached = lakeBoundsCache.get(cacheKey);
    
    if (cached && Date.now() - cached.timestamp < CACHE_TTL) {
        return cached.data;
    }
    
    try {
        const [rows] = await pool.query(
            `SELECT 
                MIN(x) as minX, MAX(x) as maxX,
                MIN(y) as minY, MAX(y) as maxY,
                MIN(z) as minZ, MAX(z) as maxZ,
                COUNT(*) as totalPoints
             FROM point_cloud_data WHERE task_id = ?`,
            [lake]
        );
        
        if (rows[0] && rows[0].totalPoints > 0) {
            const bounds = {
                minX: rows[0].minX,
                maxX: rows[0].maxX,
                minY: rows[0].minY,
                maxY: rows[0].maxY,
                minZ: rows[0].minZ,
                maxZ: rows[0].maxZ,
                totalPoints: rows[0].totalPoints
            };
            
            lakeBoundsCache.set(cacheKey, {
                data: bounds,
                timestamp: Date.now()
            });
            
            return bounds;
        }
    } catch (error) {
        console.error('获取点云边界失败:', error);
    }
    
    return null;
}

// ============ 计算自适应距离阈值 ============
function calculateAdaptiveThreshold(bounds, traceCount) {
    if (!bounds) return 120; // 默认120米，提高关联覆盖率
    
    // 计算点云空间范围
    const rangeX = bounds.maxX - bounds.minX;
    const rangeY = bounds.maxY - bounds.minY;
    const rangeZ = bounds.maxZ - bounds.minZ;
    const avgRange = (rangeX + rangeY + rangeZ) / 3;
    
    // 基于点云密度计算阈值
    const volume = rangeX * rangeY * rangeZ;
    const density = bounds.totalPoints / (volume + 1);
    
    // 综合计算阈值（以提高关联覆盖率为首要目标）
    let threshold = 120; // 基础阈值大幅提高
    
    if (avgRange > 200) {
        // 大范围场景，使用更宽松的阈值
        threshold = Math.min(300, 120 + avgRange * 0.2);
    } else if (avgRange < 50) {
        // 小范围场景，保底阈值也提高
        threshold = Math.max(60, avgRange * 1.2);
    }
    
    // 根据密度微调：稀疏点云大幅增加阈值，确保大部分点都能关联
    if (density < 0.001) {
        threshold *= 2.0; // 稀疏点云，阈值翻倍
    } else if (density < 0.01) {
        threshold *= 1.5;
    } else if (density > 0.1) {
        threshold *= 0.8; // 稠密点云，稍微收紧
    }
    
    // 兜底：保证关联率足够高
    return Math.max(50, Math.min(400, Math.round(threshold)));
}

// 2. 点云分页查询接口（带自适应阈值）
app.get('/api/points', async (req, res) => {
    const { lake, page = 1, limit = 3000, lod = 1, mode = 'depth' } = req.query;
    if (!lake) {
        return res.status(400).json({ error: '缺少 lake 参数' });
    }
    const startTime = performance.now();
    let connection;
    try {
        connection = await pool.getConnection();
        const lodStep = { 1: 1, 2: 2, 3: 4, 4: 10 }[parseInt(lod)] || 1;
        const offset = (parseInt(page) - 1) * parseInt(limit);

        // 获取点云边界和自适应阈值
        const bounds = await getPointCloudBounds(lake);
        
        // 获取轨迹数量用于计算阈值
        const [traceCountRows] = await connection.query(
            `SELECT COUNT(*) as total FROM robot_trace WHERE task_id = ?`,
            [lake]
        );
        const traceCount = traceCountRows[0]?.total || 0;
        
        // 计算自适应距离阈值
        const adaptiveThreshold = calculateAdaptiveThreshold(bounds, traceCount);
        const maxValidDistanceSq = adaptiveThreshold * adaptiveThreshold;
        
        // 动态计算传感器和轨迹数据获取量
        const sensorLimit = Math.min(50000, parseInt(limit) * 10);

        // 1. 获取点云数据
        const [pointRows] = await connection.query(`
            SELECT id, x, y, z, intensity
            FROM point_cloud_data
            WHERE task_id = ?
            AND MOD(id, ?) = 0
            ORDER BY id
            LIMIT ? OFFSET ? 
        `, [lake, lodStep, parseInt(limit), offset]);

        // 2. 分别查询轨迹和传感器
        const [traceRows] = await connection.query(`
            SELECT x, y, z, timestamp
            FROM robot_trace
            WHERE task_id = ?
            ORDER BY timestamp
            LIMIT ?
        `, [lake, sensorLimit]);

        const [sensorRows] = await connection.query(`
            SELECT timestamp, depth, temperature, dissolved_oxygen, ph, conductivity, turbidity
            FROM sensor_data
            WHERE task_id = ?
            ORDER BY timestamp
            LIMIT ?
        `, [lake, sensorLimit]); 

        console.log(`🔍 批次 ${page}: 点云${pointRows.length}个，轨迹${traceRows.length}个，传感器${sensorRows.length}个，自适应阈值${adaptiveThreshold.toFixed(1)}m`);

        // 3. 构建传感器时间映射
        const sensorMap = new Map();
        sensorRows.forEach(s => {
            const timeKey = new Date(s.timestamp).getTime();
            sensorMap.set(timeKey, {
                depth: s.depth != null ? parseFloat(s.depth) : null,
                temperature: s.temperature != null ? parseFloat(s.temperature) : null,
                dissolved_oxygen: s.dissolved_oxygen != null ? parseFloat(s.dissolved_oxygen) : null,
                ph: s.ph  != null ? parseFloat(s.ph) : null,
                conductivity: s.conductivity != null ? parseFloat(s.conductivity) : null,
                turbidity: s.turbidity != null ? parseFloat(s.turbidity) : null
            });
        });

        // 4. 构建轨迹点（关联最近的传感器）
        const tracePoints = traceRows.map(t => {
            const traceTime = new Date(t.timestamp).getTime();
            let nearestSensor = null;
            let minTimeDiff = 30000;

            for (const [sensorTime, sensorData] of sensorMap) {
                const diff = Math.abs(traceTime - sensorTime);
                if (diff < minTimeDiff) {
                    minTimeDiff = diff;
                    nearestSensor = sensorData;
                }
            }

            return {
                x: parseFloat(t.x),
                y: parseFloat(t.y),
                z: parseFloat(t.z),
                sensorData: nearestSensor
            };
        });

        // 5. 为每个点云点计算传感器数据（空间插值）
        const pointsWithSensorData = pointRows.map(point => {
            const px = parseFloat(point.x);
            const py = parseFloat(point.y);
            const pz = parseFloat(point.z);

            let nearestTrace = null;
            let minDistSq  = Infinity;

            for (const trace of tracePoints) {
                const dx = px - trace.x;
                const dy = py - trace.y;
                const dz = pz - trace.z;
                const distSq = dx * dx + dy * dy + dz * dz;

                if (distSq < minDistSq) {
                    minDistSq = distSq;
                    nearestTrace = trace;
                }
            }

            // 使用自适应距离阈值
            const sensorData = (nearestTrace && minDistSq < maxValidDistanceSq)
                ? nearestTrace.sensorData
                : null;

            return {
                id: point.id,
                x: px,
                y: py,
                z: pz, 
                intensity: parseFloat(point.intensity) || 0,
                timestamp: point.timestamp,
                sensorData: sensorData,
                nearestTraceDistance: Math.sqrt(minDistSq)
            };
        });

        const withSensorCount = pointsWithSensorData.filter(p => p.sensorData !== null).length;
        const associationRate = (withSensorCount / pointsWithSensorData.length * 100).toFixed(1);
        console.log(`✅ 批次 ${page}: ${withSensorCount}/${pointsWithSensorData.length} 点有传感器数据 (${associationRate}%)，阈值${adaptiveThreshold.toFixed(1)}m`);

        const [countRows] = await connection.query(`
            SELECT COUNT(*) as total FROM point_cloud_data WHERE task_id = ?
        `, [lake]);

        const loadTime = (performance.now() - startTime).toFixed(1);
        console.log(`⏱️ 批次 ${page} 加载耗时：${loadTime}ms`);

        res.json({
            points: pointsWithSensorData,
            meta: {
                total: countRows[0]?.total || 0,
                page: parseInt(page),
                limit: parseInt(limit),
                lod: parseInt(lod),
                hasMore: pointRows.length === parseInt(limit),
                associationRate: parseFloat(associationRate),
                adaptiveThreshold: adaptiveThreshold,
                bounds: bounds
            }
        });
    } catch (error) {
        console.error('❌ 点云查询失败:', error);
        res.status(500).json({ error: '查询失败', details: error.message });
    } finally {
        if (connection) connection.release();
    }
});


// 3. 轨迹分页查询接口（增强版 - 返回传感器数据）
app.get('/api/trajectory', async (req, res) => {
    const { lake, page = 1, limit = 2000 } = req.query;
    if (!lake) {
        return res.status(400).json({ error: '缺少 lake 参数' });
    }
    try {
        const offset = (parseInt(page) - 1) * parseInt(limit);
        
        const [rows] = await pool.query(
            `SELECT id, timestamp, x, y, z, speed, heading, battery, status 
             FROM robot_trace 
             WHERE task_id = ? 
             ORDER BY timestamp 
             LIMIT ? OFFSET ?`,
            [lake, parseInt(limit), offset]
        );
        
        const [sensorRows] = await pool.query(`
            SELECT timestamp, depth, temperature, dissolved_oxygen, ph, conductivity, turbidity
            FROM sensor_data
            WHERE task_id = ?
            ORDER BY timestamp
            LIMIT ?
        `, [lake, parseInt(limit) * 5]);
        
        const sensorMap = new Map();
        sensorRows.forEach(s => {
            const timeKey = new Date(s.timestamp).getTime();
            sensorMap.set(timeKey, {
                depth: s.depth != null ? parseFloat(s.depth) : null,
                temperature: s.temperature != null ? parseFloat(s.temperature) : null,
                dissolved_oxygen: s.dissolved_oxygen != null ? parseFloat(s.dissolved_oxygen) : null,
                ph: s.ph != null ? parseFloat(s.ph) : null,
                conductivity: s.conductivity != null ? parseFloat(s.conductivity) : null,
                turbidity: s.turbidity != null ? parseFloat(s.turbidity) : null
            });
        });
        
        const [countRows] = await pool.query(
            `SELECT COUNT(*) as total FROM robot_trace WHERE task_id = ?`,
            [lake]
        );
        
        res.json({
            trajectory: rows.map(r => {
                const traceTime = new Date(r.timestamp).getTime();
                let nearestSensor = null;
                let minTimeDiff = 30000;
                
                for (const [sensorTime, sensorData] of sensorMap) {
                    const diff = Math.abs(traceTime - sensorTime);
                    if (diff < minTimeDiff) {
                        minTimeDiff = diff;
                        nearestSensor = sensorData;
                    }
                }
                
                return {
                    id: r.id,
                    time: dayjs(r.timestamp).format('MM-DD HH:mm:ss'),
                    timestamp: r.timestamp,
                    position: { 
                        x: parseFloat(r.x), 
                        y: parseFloat(r.y), 
                        z: parseFloat(r.z) 
                    },
                    speed: parseFloat(r.speed),
                    heading: parseFloat(r.heading),
                    battery: parseFloat(r.battery),
                    status: r.status,
                    sensorData: nearestSensor
                };
            }),
            meta: {
                total: countRows[0]?.total || 0,
                page: parseInt(page),
                limit: parseInt(limit),
                hasMore: rows.length === parseInt(limit)
            }
        });
    } catch (error) {
        console.error('轨迹查询失败:', error);
        res.status(500).json({ error: '查询失败', details: error.message });
    }
});

// 4. 时间序列数据接口
app.get('/api/timeseries', async (req, res) => {
    const { lake, mode, range = '1d' } = req.query;
    if (!lake || !mode) {
        return res.status(400).json({ error: '缺少参数' });
    }
    try {
        const fieldMap = {
            'depth': 'depth', 'temperature': 'temperature',
            'dissolved_oxygen': 'dissolved_oxygen', 'ph': 'ph',
            'conductivity': 'conductivity', 'turbidity': 'turbidity'
        };
        const field = fieldMap[mode];
        if (!field) {
            return res.status(400).json({ error: '无效模式' });
        }

        const [maxTimeRows] = await pool.query(
            `SELECT MAX(timestamp) as max_time FROM sensor_data WHERE task_id = ?`,
            [lake]
        );

        const endTime = maxTimeRows[0]?.max_time ? dayjs(maxTimeRows[0].max_time) : dayjs();
        let startTime;

        switch (range) {
            case '1h': startTime = endTime.subtract(1, 'hour'); break;
            case '6h': startTime = endTime.subtract(6, 'hours'); break;
            case '1d': startTime = endTime.subtract(1, 'day'); break;
            case '3d': startTime = endTime.subtract(3, 'days'); break;
            case '7d': startTime = endTime.subtract(7, 'days'); break;
            case '30d': startTime = endTime.subtract(30, 'days'); break;
            case 'all': startTime = dayjs('2000-01-01'); break;
            default: startTime = endTime.subtract(1, 'day');
        }

        const [rows] = await pool.query(`
            SELECT timestamp, ?? as value
            FROM sensor_data
            WHERE task_id = ? AND timestamp >= ? AND timestamp <= ?
            ORDER BY timestamp ASC
            LIMIT 100000
        `, [field, lake, startTime.format('YYYY-MM-DD HH:mm:ss'), endTime.format('YYYY-MM-DD HH:mm:ss')]);

        res.json({
            data: rows.map(r => ({
                time: dayjs(r.timestamp).format('MM-DD HH:mm'),
                value: Number(r.value || 0).toFixed(2),
                timestamp: r.timestamp
            })),
            meta: { total: rows.length, range, field }
        });
    } catch (error) {
        console.error('时间序列查询失败:', error);
        res.status(500).json({ error: '查询失败', details: error.message });
    }
});

// 5a. 获取验证码
app.get('/api/captcha', (req, res) => {
    const captcha = generateCaptcha();
    const token = Date.now().toString(36) + Math.random().toString(36).substr(2);
    captchaStore.set(token, { code: captcha.code.toUpperCase(), timestamp: Date.now() });
    
    // 清理过期验证码
    const now = Date.now();
    for (const [key, val] of captchaStore) {
        if (now - val.timestamp > CAPTCHA_TTL) {
            captchaStore.delete(key);
        }
    }
    
    res.json({ token, image: captcha.image, code: captcha.code });
});

// 5b. 用户登录接口（带验证码）
app.post('/api/login', async (req, res) => {
    const { username, password, captchaToken, captchaCode } = req.body;
    
    if (!captchaToken || !captchaCode) {
        return res.status(400).json({ error: '请输入验证码' });
    }
    
    const stored = captchaStore.get(captchaToken);
    if (!stored) {
        return res.status(400).json({ error: '验证码已过期，请刷新' });
    }
    
    if (stored.code !== captchaCode.toUpperCase()) {
        return res.status(400).json({ error: '验证码错误' });
    }
    
    // 验证通过后删除验证码
    captchaStore.delete(captchaToken);
    
    try {
        const [rows] = await pool.query('SELECT username, password, role FROM users WHERE username = ?', [username]);
        if (rows.length === 0 || rows[0].password !== password) {
            return res.status(401).json({ error: '用户名或密码错误' });
        }
        
        res.json({ 
            success: true, 
            username: rows[0].username,
            role: rows[0].role || 'operator'
        });
    } catch (error) {
        console.error('登录失败:', error);
        res.status(500).json({ error: '登录服务异常', details: error.message });
    }
});

// 5c. 用户认证接口（保留兼容）
app.get('/api/users', async (req, res) => {
    try {
        const [rows] = await pool.query('SELECT username, password FROM users');
        const users = {};
        rows.forEach(r => {
            users[r.username] = r.password;
        });
        res.json(users);
    } catch (error) {
        console.error('获取用户数据失败:', error);
        res.status(500).json({ error: '获取用户数据失败', details: error.message });
    }
});

// 6. 健康检查
app.get('/api/health', async (req, res) => {
    try {
        await pool.query('SELECT 1');
        res.json({ status: 'healthy', database: 'connected' });
    } catch (error) {
        res.status(500).json({ status: 'unhealthy', database: 'disconnected' });
    }
});

// 7. AI助手接口 - 调用阿里云API
app.post('/api/ai/chat', async (req, res) => {
    const { message, lakeName, sensorData, lakeStats, context } = req.body;
    
    if (!message) {
        return res.status(400).json({ error: '缺少消息内容' });
    }

    try {
        // 如果前端没有提供lakeStats，尝试从数据库获取
        let dbStats = lakeStats;
        if (!dbStats && lakeName) {
            try {
                const [sensorStats] = await pool.query(`
                    SELECT 
                        AVG(depth) as avgDepth,
                        MIN(depth) as minDepth,
                        MAX(depth) as maxDepth,
                        AVG(temperature) as avgTemp,
                        MIN(temperature) as minTemp,
                        MAX(temperature) as maxTemp,
                        AVG(dissolved_oxygen) as avgDO,
                        MIN(dissolved_oxygen) as minDO,
                        MAX(dissolved_oxygen) as maxDO,
                        AVG(ph) as avgPH,
                        MIN(ph) as minPH,
                        MAX(ph) as maxPH,
                        AVG(conductivity) as avgCond,
                        MIN(conductivity) as minCond,
                        MAX(conductivity) as maxCond,
                        AVG(turbidity) as avgTurb,
                        MIN(turbidity) as minTurb,
                        MAX(turbidity) as maxTurb,
                        COUNT(*) as totalRecords,
                        MIN(timestamp) as firstRecord,
                        MAX(timestamp) as lastRecord
                    FROM sensor_data 
                    WHERE task_id = ?
                `, [lakeName]);

                const [pointStats] = await pool.query(`
                    SELECT 
                        COUNT(*) as totalPoints,
                        AVG(x) as avgX,
                        AVG(y) as avgY,
                        AVG(z) as avgZ,
                        MIN(z) as minZ,
                        MAX(z) as maxZ,
                        STDDEV(z) as depthVariance
                    FROM point_cloud_data 
                    WHERE task_id = ?
                `, [lakeName]);

                dbStats = {
                    sensorStats: sensorStats[0],
                    pointStats: pointStats[0]
                };
            } catch (dbError) {
                console.warn('获取数据库统计失败:', dbError.message);
            }
        }

        // 构建系统提示词
        const systemPrompt = `你是"蓝脊灵豚"生态保护 AI 助手，专注于湖泊生态系统的保护、修复和碳汇计算。你必须基于下方提供的**真实监测数据**进行分析，杜绝泛泛而谈。

【专业领域】
1. 碳汇计算与评估（湖泊蓝碳、沉积物有机碳）<br>
2. 水质分析与生态健康（富营养化、溶解氧、温度分层）<br>
3. 生态修复建议（植被恢复、底泥治理）<br>
4. 数据分析与解读（点云地形、传感器趋势）

【当前监测数据】
${lakeName ? `监测湖泊：${lakeName}` : '未选择湖泊'}<br>
${sensorData ? `
实时传感器数据：<br>
• 深度：${sensorData.depth != null ? Number(sensorData.depth).toFixed(2) : 'N/A'} m<br>
• 温度：${sensorData.temperature != null ? Number(sensorData.temperature).toFixed(2) : 'N/A'} °C<br>
• 溶解氧：${sensorData.dissolved_oxygen != null ? Number(sensorData.dissolved_oxygen).toFixed(2) : 'N/A'} mg/L<br>
• pH：${sensorData.ph != null ? Number(sensorData.ph).toFixed(2) : 'N/A'}<br>
• 电导率：${sensorData.conductivity != null ? Number(sensorData.conductivity).toFixed(2) : 'N/A'} μS/cm<br>
• 浊度：${sensorData.turbidity != null ? Number(sensorData.turbidity).toFixed(2) : 'N/A'} NTU` : '传感器数据：暂无有效数据'}<br>
${dbStats?.sensorStats ? `
历史统计数据：<br>
• 数据记录数：${dbStats.sensorStats.totalRecords || 0} 条<br>
• 深度范围：${dbStats.sensorStats.minDepth != null ? Number(dbStats.sensorStats.minDepth).toFixed(2) : 'N/A'} - ${dbStats.sensorStats.maxDepth != null ? Number(dbStats.sensorStats.maxDepth).toFixed(2) : 'N/A'} m<br>
• 温度范围：${dbStats.sensorStats.minTemp != null ? Number(dbStats.sensorStats.minTemp).toFixed(2) : 'N/A'} - ${dbStats.sensorStats.maxTemp != null ? Number(dbStats.sensorStats.maxTemp).toFixed(2) : 'N/A'} °C<br>
• 溶解氧范围：${dbStats.sensorStats.minDO != null ? Number(dbStats.sensorStats.minDO).toFixed(2) : 'N/A'} - ${dbStats.sensorStats.maxDO != null ? Number(dbStats.sensorStats.maxDO).toFixed(2) : 'N/A'} mg/L<br>
• pH范围：${dbStats.sensorStats.minPH != null ? Number(dbStats.sensorStats.minPH).toFixed(2) : 'N/A'} - ${dbStats.sensorStats.maxPH != null ? Number(dbStats.sensorStats.maxPH).toFixed(2) : 'N/A'}<br>
• 电导率范围：${dbStats.sensorStats.minCond != null ? Number(dbStats.sensorStats.minCond).toFixed(2) : 'N/A'} - ${dbStats.sensorStats.maxCond != null ? Number(dbStats.sensorStats.maxCond).toFixed(2) : 'N/A'} μS/cm<br>
• 浊度范围：${dbStats.sensorStats.minTurb != null ? Number(dbStats.sensorStats.minTurb).toFixed(2) : 'N/A'} - ${dbStats.sensorStats.maxTurb != null ? Number(dbStats.sensorStats.maxTurb).toFixed(2) : 'N/A'} NTU<br>
• 监测时间：${dbStats.sensorStats.firstRecord ? new Date(dbStats.sensorStats.firstRecord).toISOString().split('T')[0] : 'N/A'} 至 ${dbStats.sensorStats.lastRecord ? new Date(dbStats.sensorStats.lastRecord).toISOString().split('T')[0] : 'N/A'}` : ''}

${dbStats?.pointStats ? `
点云地形统计：<br>
• 点云总数：${dbStats.pointStats.totalPoints || 0} 个<br>
• 平均深度(Z)：${dbStats.pointStats.avgZ != null ? Number(dbStats.pointStats.avgZ).toFixed(2) : 'N/A'} m<br>
• 深度范围：${dbStats.pointStats.minZ != null ? Number(dbStats.pointStats.minZ).toFixed(2) : 'N/A'} - ${dbStats.pointStats.maxZ != null ? Number(dbStats.pointStats.maxZ).toFixed(2) : 'N/A'} m<br>
• 深度标准差：${dbStats.pointStats.depthVariance != null ? Number(dbStats.pointStats.depthVariance).toFixed(2) : 'N/A'} m` : ''}

【回答要求 - 必须遵守】
1. 言之有物（最重要）：<br>
   • 所有分析必须引用上方具体数据，禁止空泛描述<br>
   • 每个结论后紧跟支撑数据，如"温度偏低（2.3 °C，参考值 4-10 °C）"<br>
   • 若数据不足，明确告知"当前数据仅包含 X 条记录，结论置信度有限"<br><br>
2. 格式规范：<br>
   • 使用标准中文标点（，。；：！？）<br>
   • 每个要点单独一行，用 <br> 换行，段落间空一行<br>
   • 列表项前用 • 或 1. 2. 3.，禁止用 # - * · 等符号<br>
   • 禁止在数字前后加星号 **，如"3.25 °C"即可，不要写成"**3.25 °C**"<br><br>
3. 公式表达：<br>
   • 用中文描述变量，如：碳储量 = 水域面积 × 单位面积碳储量 × 碳埋藏效率<br>
   • 禁止 LaTeX 语法，用"×"表示乘法<br><br>
4. 关键内容高亮（必须执行）：<br>
   • 所有重要结论用【结论】标记开头<br>
   • 所有风险提示用【注意】标记开头<br>
   • 所有具体建议用【建议】标记开头<br>
   • 超出正常范围的数据标注异常，如：pH 9.2（偏高，参考 6.5-8.5）<br><br>
5. 回答结构：<br>
   • 开头：1 句话核心结论（含具体数据）<br>
   • 中间：分点说明（每点用 <br> 换行），每点必须有数据支撑<br>
   • 结尾：2-3 条可操作建议，明确操作步骤`;

        const requestBody = {
    model: 'qwen-plus',
    messages: [
        { role: 'system', content: systemPrompt },
        ...(context || []),
        { role: 'user', content: message }
    ],
    temperature: 0.5,            // 降低随机性，格式更稳定
    max_tokens: 1000,            // 限制长度，加快响应
    top_p: 0.8                   // 进一步控制多样性
};

        console.log('🤖 AI请求:', { message: message.substring(0, 50) + '...', lakeName });

        const response = await fetch(`${ALIYUN_API_CONFIG.baseUrl}/chat/completions`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${ALIYUN_API_CONFIG.apiKey}`
            },
            body: JSON.stringify(requestBody)
        });

        if (!response.ok) {
            const errorData = await response.text();
            console.error('❌ 阿里云API错误:', response.status, errorData);
            throw new Error(`API请求失败: ${response.status}`);
        }

        const data = await response.json();
        const aiResponse = data.choices?.[0]?.message?.content;

        if (!aiResponse) {
            throw new Error('AI响应为空');
        }

        console.log('✅ AI响应成功:', aiResponse.substring(0, 50) + '...');

        res.json({
            success: true,
            response: aiResponse,
            usage: data.usage
        });

    } catch (error) {
        console.error('❌ AI接口错误:', error);
        res.status(500).json({ 
            error: 'AI服务暂时不可用', 
            details: error.message,
            fallback: true
        });
    }
});

// ==================== Shapefile 导出工具函数 ====================

/**
 * 二分查找最近时间的传感器数据（O(log n)）
 */
function buildSortedSensors(sensorRows) {
    const arr = sensorRows.map(s => ({
        time: new Date(s.timestamp).getTime(),
        data: s
    })).sort((a, b) => a.time - b.time);
    return arr;
}

function findNearestSensor(sortedSensors, targetTime) {
    if (!sortedSensors.length) return null;
    let lo = 0, hi = sortedSensors.length - 1;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (sortedSensors[mid].time < targetTime) lo = mid + 1;
        else hi = mid;
    }
    let best = sortedSensors[lo];
    let minDiff = Math.abs(best.time - targetTime);
    if (lo > 0) {
        const diff = Math.abs(sortedSensors[lo - 1].time - targetTime);
        if (diff < minDiff) {
            best = sortedSensors[lo - 1];
            minDiff = diff;
        }
    }
    return best.data;
}

/**
 * 写入 .shp 主文件记录（PointZ，ShapeType = 11）
 * 每条记录：8 byte header + 36 byte content = 44 bytes
 */
function writeShpRecord(buf, offset, recordNum, x, y, z) {
    // Record header (big-endian)
    buf.writeInt32BE(recordNum, offset);      // Record number (1-based)
    buf.writeInt32BE(18, offset + 4);          // Content length in 16-bit words (36/2 = 18)
    // Content (little-endian)
    buf.writeInt32LE(11, offset + 8);          // ShapeType = PointZ
    buf.writeDoubleLE(x, offset + 12);         // X
    buf.writeDoubleLE(y, offset + 20);         // Y
    buf.writeDoubleLE(z, offset + 28);         // Z
    buf.writeDoubleLE(NaN, offset + 36);       // M (optional, NaN)
}

/**
 * 写入 .shx 索引记录
 * 每条记录：4 byte offset + 4 byte length = 8 bytes
 */
function writeShxRecord(buf, offset, shpOffsetWords, contentLenWords) {
    buf.writeInt32BE(shpOffsetWords, offset);
    buf.writeInt32BE(contentLenWords, offset + 4);
}

/**
 * 构建 .shp / .shx 文件头（100 bytes）
 */
function buildShpShxHeader(recordCount, fileLenWords, xmin, ymin, xmax, ymax, zmin, zmax) {
    const buf = Buffer.alloc(100);
    buf.writeInt32BE(9994, 0);           // File code
    buf.fill(0, 4, 24);                   // Unused
    buf.writeInt32BE(fileLenWords, 24);   // File length in 16-bit words
    buf.writeInt32LE(1000, 28);           // Version
    buf.writeInt32LE(11, 32);             // ShapeType = PointZ
    buf.writeDoubleLE(xmin, 36);          // Xmin
    buf.writeDoubleLE(ymin, 44);          // Ymin
    buf.writeDoubleLE(xmax, 52);          // Xmax
    buf.writeDoubleLE(ymax, 60);          // Ymax
    buf.writeDoubleLE(zmin, 68);          // Zmin
    buf.writeDoubleLE(zmax, 76);          // Zmax
    buf.writeDoubleLE(NaN, 84);           // Mmin
    buf.writeDoubleLE(NaN, 92);           // Mmax
    return buf;
}

/**
 * 构建 DBF 属性表
 * 字段格式：dBASE III+，Numeric
 */
function buildDbfBuffer(recordCount, fields, recordsGenerator) {
    const headerSize = 32 + fields.length * 32 + 1; // +1 for header terminator 0x0D
    const recordSize = 1 + fields.reduce((sum, f) => sum + f.len, 0);
    const buf = Buffer.alloc(headerSize + recordSize * recordCount + 1); // +1 for EOF 0x1A
    let pos = 0;

    // --- File Header (32 bytes) ---
    const now = new Date();
    buf.writeUInt8(0x03, pos++);                      // Version: dBASE III+
    buf.writeUInt8(now.getFullYear() - 1900, pos++);  // YY
    buf.writeUInt8(now.getMonth() + 1, pos++);        // MM
    buf.writeUInt8(now.getDate(), pos++);             // DD
    buf.writeUInt32LE(recordCount, pos); pos += 4;
    buf.writeUInt16LE(headerSize, pos); pos += 2;
    buf.writeUInt16LE(recordSize, pos); pos += 2;
    buf.fill(0, pos, pos + 20); pos += 20;            // Reserved

    // --- Field Descriptors (32 bytes each) ---
    for (const f of fields) {
        const name = f.name.padEnd(11, '\0').substring(0, 11);
        buf.write(name, pos, 11, 'ascii'); pos += 11;
        buf.writeUInt8(f.type.charCodeAt(0), pos++);
        buf.writeUInt32LE(0, pos); pos += 4;          // Field data address
        buf.writeUInt8(f.len, pos++);
        buf.writeUInt8(f.dec, pos++);
        buf.fill(0, pos, pos + 14); pos += 14;        // Reserved
    }
    buf.writeUInt8(0x0D, pos++);                      // Header terminator

    // --- Records ---
    let recordIdx = 0;
    for (const values of recordsGenerator) {
        buf.writeUInt8(0x20, pos++);                  // Valid record flag
        for (let i = 0; i < fields.length; i++) {
            const f = fields[i];
            const v = values[i];
            let str;
            if (v === null || v === undefined || (typeof v === 'number' && isNaN(v))) {
                str = ' '.repeat(f.len);
            } else if (f.type === 'N' || f.type === 'F') {
                const num = Number(v).toFixed(f.dec);
                str = num.padStart(f.len, ' ').substring(0, f.len);
            } else {
                str = String(v).padEnd(f.len, ' ').substring(0, f.len);
            }
            buf.write(str, pos, f.len, 'ascii');
            pos += f.len;
        }
        recordIdx++;
    }

    buf.writeUInt8(0x1A, pos++);                      // EOF marker
    return buf;
}

// ==================== 7b. 导出点云数据接口（Shapefile）====================
app.get('/api/export/points', async (req, res) => {
    const { lake } = req.query;
    if (!lake) {
        return res.status(400).json({ error: '缺少 lake 参数' });
    }

    const tmpDir = path.join(__dirname, 'tmp_export_' + Date.now());
    let connection;
    const startTime = Date.now();

    try {
        connection = await pool.getConnection();

        // 1) 获取点云总数和空间边界
        const [boundsRows] = await connection.query(
            `SELECT COUNT(*) AS total,
                    MIN(x) AS minX, MAX(x) AS maxX,
                    MIN(y) AS minY, MAX(y) AS maxY,
                    MIN(z) AS minZ, MAX(z) AS maxZ
             FROM point_cloud_data WHERE task_id = ?`,
            [lake]
        );
        const total = boundsRows[0]?.total || 0;
        if (total === 0) {
            return res.status(404).json({ error: '该任务暂无点云数据' });
        }

        // 2) SQL JOIN 一次性获取带传感器数据的轨迹点（数据库层面完成时间关联）
        const [traceRows] = await connection.query(
            `SELECT t.x, t.y, t.z,
                    s.depth, s.temperature, s.dissolved_oxygen, s.ph, s.conductivity, s.turbidity
             FROM robot_trace t
             LEFT JOIN sensor_data s ON t.robot_id = s.robot_id AND t.timestamp = s.timestamp
             WHERE t.task_id = ?`,
            [lake]
        );

        // 3) 构建 3D 空间哈希索引（用于快速空间关联）
        const spatialIndex = new Map();
        let gridSize = 50;
        const finalBounds = boundsRows[0];

        if (traceRows.length > 0) {
            const rangeX = Math.max(1, finalBounds.maxX - finalBounds.minX);
            const rangeY = Math.max(1, finalBounds.maxY - finalBounds.minY);
            const rangeZ = Math.max(1, finalBounds.maxZ - finalBounds.minZ);
            const volume = rangeX * rangeY * rangeZ;
            // 目标平均每网格约 2 个轨迹点
            gridSize = Math.max(5, Math.cbrt(volume / (traceRows.length * 2)));
        }

        for (const t of traceRows) {
            const gx = Math.floor(t.x / gridSize);
            const gy = Math.floor(t.y / gridSize);
            const gz = Math.floor(t.z / gridSize);
            const key = `${gx},${gy},${gz}`;
            if (!spatialIndex.has(key)) spatialIndex.set(key, []);
            spatialIndex.get(key).push({
                x: parseFloat(t.x),
                y: parseFloat(t.y),
                z: parseFloat(t.z),
                depth: t.depth,
                temperature: t.temperature,
                dissolved_oxygen: t.dissolved_oxygen,
                ph: t.ph,
                conductivity: t.conductivity,
                turbidity: t.turbidity
            });
        }

        // 4) 创建临时目录和文件，流式写入
        fs.mkdirSync(tmpDir, { recursive: true });
        const safeName = String(lake).replace(/[^a-zA-Z0-9_\u4e00-\u9fa5]/g, '_');
        const shpPath = path.join(tmpDir, `${safeName}.shp`);
        const shxPath = path.join(tmpDir, `${safeName}.shx`);
        const dbfPath = path.join(tmpDir, `${safeName}.dbf`);

        const shpFd = fs.openSync(shpPath, 'w');
        const shxFd = fs.openSync(shxPath, 'w');
        const dbfFd = fs.openSync(dbfPath, 'w');

        // 先写入占位 header（100 字节）
        fs.writeSync(shpFd, Buffer.alloc(100), 0, 100, 0);
        fs.writeSync(shxFd, Buffer.alloc(100), 0, 100, 0);

        // DBF 字段定义（包含传感器属性）
        const dbfFields = [
            { name: 'ID',         type: 'N', len: 10, dec: 0 },
            { name: 'X',          type: 'F', len: 12, dec: 4 },
            { name: 'Y',          type: 'F', len: 12, dec: 4 },
            { name: 'Z',          type: 'F', len: 12, dec: 4 },
            { name: 'INTENSITY',  type: 'F', len: 10, dec: 2 },
            { name: 'DEPTH',      type: 'F', len: 10, dec: 4 },
            { name: 'TEMP',       type: 'F', len: 10, dec: 4 },
            { name: 'DO',         type: 'F', len: 10, dec: 4 },
            { name: 'PH',         type: 'F', len:  8, dec: 2 },
            { name: 'CONDUCT',    type: 'F', len: 12, dec: 2 },
            { name: 'TURB',       type: 'F', len: 10, dec: 4 }
        ];
        const dbfHeaderSize = 32 + dbfFields.length * 32 + 1;
        const dbfRecordSize = 1 + dbfFields.reduce((s, f) => s + f.len, 0);

        // 构建并写入 DBF header（记录数先占位为 0）
        const dbfHeader = Buffer.alloc(dbfHeaderSize);
        const now = new Date();
        dbfHeader.writeUInt8(0x03, 0);
        dbfHeader.writeUInt8(now.getFullYear() - 1900, 1);
        dbfHeader.writeUInt8(now.getMonth() + 1, 2);
        dbfHeader.writeUInt8(now.getDate(), 3);
        dbfHeader.writeUInt32LE(0, 4);   // 记录数占位
        dbfHeader.writeUInt16LE(dbfHeaderSize, 8);
        dbfHeader.writeUInt16LE(dbfRecordSize, 10);
        dbfHeader.fill(0, 12, 32);

        for (let fi = 0; fi < dbfFields.length; fi++) {
            const f = dbfFields[fi];
            const pos = 32 + fi * 32;
            const name = f.name.padEnd(11, '\0').substring(0, 11);
            dbfHeader.write(name, pos, 11, 'ascii');
            dbfHeader.writeUInt8(f.type.charCodeAt(0), pos + 11);
            dbfHeader.writeUInt32LE(0, pos + 12);
            dbfHeader.writeUInt8(f.len, pos + 16);
            dbfHeader.writeUInt8(f.dec, pos + 17);
            dbfHeader.fill(0, pos + 18, pos + 32);
        }
        dbfHeader.writeUInt8(0x0D, dbfHeaderSize - 1);
        fs.writeSync(dbfFd, dbfHeader, 0, dbfHeaderSize, 0);

        // 5) 键集分页查询点云（无 OFFSET，避免大偏移扫描）+ 流式写入
        const BATCH_SIZE = 50000;
        let lastId = 0;
        let shpPos = 100;
        let shxPos = 100;
        let dbfPos = dbfHeaderSize;
        let recordNum = 1;
        let processedCount = 0;

        while (true) {
            const [pointRows] = await connection.query(
                `SELECT id, x, y, z, intensity FROM point_cloud_data
                 WHERE task_id = ? AND id > ? ORDER BY id LIMIT ?`,
                [lake, lastId, BATCH_SIZE]
            );
            if (!pointRows.length) break;

            // 每批预分配 Buffer，减少 fs.writeSync 调用次数
            const batchShp = Buffer.alloc(pointRows.length * 44);
            const batchShx = Buffer.alloc(pointRows.length * 8);
            const batchDbf = Buffer.alloc(pointRows.length * dbfRecordSize);
            let bShp = 0, bShx = 0, bDbf = 0;

            for (const p of pointRows) {
                const px = parseFloat(p.x);
                const py = parseFloat(p.y);
                const pz = parseFloat(p.z);

                // 空间哈希关联：只搜索周围 3x3x3 = 27 个网格
                let sensor = null;
                if (traceRows.length > 0) {
                    const gx = Math.floor(px / gridSize);
                    const gy = Math.floor(py / gridSize);
                    const gz = Math.floor(pz / gridSize);
                    let minDistSq = Infinity;

                    for (let dx = -1; dx <= 1; dx++) {
                        for (let dy = -1; dy <= 1; dy++) {
                            for (let dz = -1; dz <= 1; dz++) {
                                const points = spatialIndex.get(`${gx + dx},${gy + dy},${gz + dz}`);
                                if (!points) continue;
                                for (const tp of points) {
                                    const d = (px - tp.x) ** 2 + (py - tp.y) ** 2 + (pz - tp.z) ** 2;
                                    if (d < minDistSq) {
                                        minDistSq = d;
                                        sensor = tp;
                                    }
                                }
                            }
                        }
                    }
                }

                // --- .shp record ---
                batchShp.writeInt32BE(recordNum, bShp);
                batchShp.writeInt32BE(18, bShp + 4);
                batchShp.writeInt32LE(11, bShp + 8);
                batchShp.writeDoubleLE(px, bShp + 12);
                batchShp.writeDoubleLE(py, bShp + 20);
                batchShp.writeDoubleLE(pz, bShp + 28);
                batchShp.writeDoubleLE(NaN, bShp + 36);
                bShp += 44;

                // --- .shx record ---
                batchShx.writeInt32BE(50 + (recordNum - 1) * 22, bShx);
                batchShx.writeInt32BE(18, bShx + 4);
                bShx += 8;

                // --- .dbf record ---
                batchDbf.writeUInt8(0x20, bDbf);
                let dp = bDbf + 1;
                const values = [
                    p.id, px, py, pz, p.intensity ?? null,
                    sensor?.depth ?? null,
                    sensor?.temperature ?? null,
                    sensor?.dissolved_oxygen ?? null,
                    sensor?.ph ?? null,
                    sensor?.conductivity ?? null,
                    sensor?.turbidity ?? null
                ];
                for (let i = 0; i < dbfFields.length; i++) {
                    const f = dbfFields[i];
                    const v = values[i];
                    let str;
                    if (v === null || v === undefined || (typeof v === 'number' && isNaN(v))) {
                        str = ' '.repeat(f.len);
                    } else {
                        const num = Number(v).toFixed(f.dec);
                        str = num.padStart(f.len, ' ').substring(0, f.len);
                    }
                    batchDbf.write(str, dp, f.len, 'ascii');
                    dp += f.len;
                }
                bDbf += dbfRecordSize;

                recordNum++;
            }

            fs.writeSync(shpFd, batchShp, 0, bShp, shpPos);
            fs.writeSync(shxFd, batchShx, 0, bShx, shxPos);
            fs.writeSync(dbfFd, batchDbf, 0, bDbf, dbfPos);

            shpPos += bShp;
            shxPos += bShx;
            dbfPos += bDbf;

            lastId = pointRows[pointRows.length - 1].id;
            processedCount += pointRows.length;
        }

        // DBF EOF 标记
        const eofBuf = Buffer.alloc(1);
        eofBuf.writeUInt8(0x1A, 0);
        fs.writeSync(dbfFd, eofBuf, 0, 1, dbfPos);

        // 6) 回头写入正确的文件头
        const count = processedCount;

        const shpHeader = buildShpShxHeader(count, 50 + count * 22,
            finalBounds.minX, finalBounds.minY, finalBounds.maxX, finalBounds.maxY,
            finalBounds.minZ, finalBounds.maxZ);
        fs.writeSync(shpFd, shpHeader, 0, 100, 0);
        fs.closeSync(shpFd);

        const shxHeader = buildShpShxHeader(count, 50 + count * 4,
            finalBounds.minX, finalBounds.minY, finalBounds.maxX, finalBounds.maxY,
            finalBounds.minZ, finalBounds.maxZ);
        fs.writeSync(shxFd, shxHeader, 0, 100, 0);
        fs.closeSync(shxFd);

        // 更新 DBF 记录数
        const countBuf = Buffer.alloc(4);
        countBuf.writeUInt32LE(count, 0);
        fs.writeSync(dbfFd, countBuf, 0, 4, 4);
        fs.closeSync(dbfFd);

        // 7) 打包 ZIP
        const zip = new JSZip();
        zip.file(`${safeName}.shp`, fs.readFileSync(shpPath));
        zip.file(`${safeName}.shx`, fs.readFileSync(shxPath));
        zip.file(`${safeName}.dbf`, fs.readFileSync(dbfPath));
        zip.file(`${safeName}.prj`, 'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],PRIMEM["Greenwich",0],UNIT["Degree",0.017453292519943295]]');

        const zipBuffer = await zip.generateAsync({ type: 'nodebuffer', compression: 'DEFLATE' });

        res.setHeader('Content-Type', 'application/zip');
        res.setHeader('Content-Disposition', `attachment; filename="${encodeURIComponent(lake)}_pointcloud.zip"`);
        res.send(zipBuffer);

        const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
        console.log(`✅ SHP 导出成功：${lake}，${count.toLocaleString()} 点，耗时 ${elapsed}s，ZIP ${(zipBuffer.length / 1024 / 1024).toFixed(2)} MB`);

    } catch (error) {
        console.error('❌ 导出 SHP 失败:', error);
        if (!res.headersSent) {
            res.status(500).json({ error: '导出失败', details: error.message });
        }
    } finally {
        if (connection) connection.release();
        // 清理临时文件
        try {
            if (fs.existsSync(tmpDir)) {
                fs.rmSync(tmpDir, { recursive: true, force: true });
            }
        } catch (e) {
            console.warn('清理临时文件失败:', e.message);
        }
    }
});

// 8. 获取湖泊统计数据（用于AI分析）
app.get('/api/lake-stats', async (req, res) => {
    const { lake } = req.query;
    if (!lake) {
        return res.status(400).json({ error: '缺少 lake 参数' });
    }

    try {
        const [sensorStats] = await pool.query(`
            SELECT 
                AVG(depth) as avgDepth,
                MIN(depth) as minDepth,
                MAX(depth) as maxDepth,
                AVG(temperature) as avgTemp,
                MIN(temperature) as minTemp,
                MAX(temperature) as maxTemp,
                AVG(dissolved_oxygen) as avgDO,
                MIN(dissolved_oxygen) as minDO,
                MAX(dissolved_oxygen) as maxDO,
                AVG(ph) as avgPH,
                MIN(ph) as minPH,
                MAX(ph) as maxPH,
                AVG(conductivity) as avgCond,
                MIN(conductivity) as minCond,
                MAX(conductivity) as maxCond,
                AVG(turbidity) as avgTurb,
                MIN(turbidity) as minTurb,
                MAX(turbidity) as maxTurb,
                COUNT(*) as totalRecords,
                MIN(timestamp) as firstRecord,
                MAX(timestamp) as lastRecord
            FROM sensor_data 
            WHERE task_id = ?
        `, [lake]);

        const [latestData] = await pool.query(`
            SELECT * FROM sensor_data 
            WHERE task_id = ? 
            ORDER BY timestamp DESC 
            LIMIT 1
        `, [lake]);

        const [pointStats] = await pool.query(`
            SELECT 
                COUNT(*) as totalPoints,
                AVG(x) as avgX,
                AVG(y) as avgY,
                AVG(z) as avgZ,
                MIN(z) as minZ,
                MAX(z) as maxZ,
                STDDEV(z) as depthVariance
            FROM point_cloud_data 
            WHERE task_id = ?
        `, [lake]);

        res.json({
            lake: lake,
            sensorStats: sensorStats[0],
            latestData: latestData[0],
            pointStats: pointStats[0]
        });

    } catch (error) {
        console.error('❌ 获取湖泊统计失败:', error);
        res.status(500).json({ error: '获取统计失败', details: error.message });
    }
});

// 启动服务器
const PORT = process.env.PORT || 3002;
const HOST = '0.0.0.0';

app.listen(PORT, HOST, () => {
    console.log(`✅ 后端已启动：http://${HOST}:${PORT}`);
    console.log(`📡 外网访问地址：http://47.108.232.40`);
    console.log(`🤖 AI助手已启用 - 阿里云API`);
});
