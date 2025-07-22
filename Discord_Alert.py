import discord
from discord.ext import commands, tasks
from DatabaseReader import DatabaseReader
import atexit
import asyncio

# Path to SQLite DB
db_path = r"'DATABASE"

# Constants
CHANNEL_ID = 'SECRET'
LN2_Level_ALERT_THRESHOLD = 1.250  # volts

# Discord bot setup
intents = discord.Intents.default()
intents.message_content = True
bot = commands.Bot(command_prefix="!", intents=intents)

# Global reference to avoid duplicate alerts
alert_triggered = False

#Startup
@bot.event
async def on_ready():
    print(f"✅ Bot is online as {bot.user}")
    channel = bot.get_channel(CHANNEL_ID)
    if channel:
        await channel.send("👁️👄👁️ Always watching...")
    check_thermocouple.start()  # Start background monitoring
    check_ColdHeads.start()


async def send_shutdown_alert():
    await bot.wait_until_ready()
    channel = bot.get_channel(CHANNEL_ID)
    if channel:
        await channel.send(content=":wave: Bye~ \n https://media.tenor.com/Kx_b2iCSraUAAAAC/peace-out.gif")

@bot.event
async def on_disconnect():
    print("🔌 Bot disconnected from Discord. :headstone:")

#Shut down the bot given ID
@bot.command(name='ShutDown')
async def shutdown_bot(ctx):
    if ctx.channel.id != CHANNEL_ID:
        return

    allowed_user_ids = ['USER_ID']  
    if ctx.author.id not in allowed_user_ids:
        await ctx.send("❌ You are not authorized to shut me down.")
        return

    await ctx.send("⚠️ Shutting down bot...")
    await send_shutdown_alert()
    await bot.close()

#List Help commands.
@bot.command(name='Commands')
async def help_command(ctx):
    if ctx.channel.id != CHANNEL_ID:
        return

    help_message = (
        "📖 **Available Commands**:\n"
        "• `!LN2` – Show the current LN2 thermocouple voltage.\n"
        "• `!ShutDown` – Shut down the bot (restricted access).\n"
        "• `!Update` Give a status report.\n"
        "• `!Commands` – Show this help message.\n"

    )
    await ctx.send(help_message)


@bot.command(name='LN2')
async def Alert_LN2(ctx):
    if ctx.channel.id != CHANNEL_ID:
        return

    try:
        reader = DatabaseReader(db_path)
        raw_V = reader.get_latest_value("Labjack", "thermocouple")
        reader.close()

        if raw_V is None:
            await ctx.send("⚠️ No thermocouple data found.")
            return

        await ctx.send(f"🌡️ LN2 Thermocouple Reading:\n• Raw Voltage: `{raw_V:.3f} V`")

    except Exception as e:
        await ctx.send(f"❌ Error retrieving temperature: `{str(e)}`")

#Check the status of LN2 in purifier 
@tasks.loop(seconds=60.0)  
async def check_thermocouple():
    global alert_triggered
    try:
        reader = DatabaseReader(db_path)
        raw_V = reader.get_latest_value("Labjack", "thermocouple")
        reader.close()

        if raw_V is not None and raw_V >= LN2_Level_ALERT_THRESHOLD and not alert_triggered:
            channel = bot.get_channel(CHANNEL_ID)
            if channel:
                await channel.send("🚨 **ALERT:** Purifier needs to be refilled! Message !Filled when refilled!")
                alert_triggered = True  # avoid spam
        elif raw_V is not None and raw_V < LN2_Level_ALERT_THRESHOLD:
            alert_triggered = False  # reset when value drops

    except Exception as e:
        print(f"[Monitor Error] {e}")

#Check the status of the ColdHeads
alert_triggered_ColdHeads = False
@tasks.loop(seconds=60.0)
async def check_ColdHeads():
    global alert_triggered_ColdHeads

    SENSOR_NAMES = ["ti501_ai", "ti502_ai", "ti503_ai", "ti504_ai", "ti505_ai"]
    HIGH_TEMP = 7.0
    LOW_TEMP = 3.0
    OUT_OF_RANGE = lambda v: v is not None and (v > HIGH_TEMP or v < LOW_TEMP)

    try:
        reader = DatabaseReader(db_path)
        triggered = []

        for sensor in SENSOR_NAMES:
            value = reader.get_latest_value("HMI", sensor)
            if OUT_OF_RANGE(value):
                triggered.append((sensor, value))

        reader.close()

        if triggered and not alert_triggered_ColdHeads:
            channel = bot.get_channel(CHANNEL_ID)
            if channel:
                msg = "🚨 **ColdHead Temp ALERT** 🚨\n"
                for name, value in triggered:
                    status = "🥶 Too Cold!" if value < LOW_TEMP else "🥵 Too Hot!"
                    msg += f"• `{name}` = `{value:.2f}` V {status}\n"

                msg += "\nAny value < `3.0` or > `5.0` is considered unsafe!"
                await channel.send(msg)
                alert_triggered_ColdHeads = True

        elif not triggered:
            alert_triggered_ColdHeads = False

    except Exception as e:
        print(f"[ColdHead Monitor Error] {e}")

#####Give Status Update#####


@bot.command(name='Update')
async def status_update(ctx):
    # if ctx.channel.id != CHANNEL_ID:
    #     return

    try:
        reader = DatabaseReader(db_path)

        # Get values from HMI
        pt501 = reader.get_latest_value("HMI", "pt501_ai")
        pt502 = reader.get_latest_value("HMI", "pt502_ai")
        ti_values = [
            reader.get_latest_value("HMI", sensor)
            for sensor in ["ti501_ai", "ti502_ai", "ti503_ai", "ti504_ai", "ti505_ai"]
        ]
        fc501 = reader.get_latest_value("HMI", "fc501_ai")
        fc502 = reader.get_latest_value("HMI", "fc502_ai")
        lit501 = reader.get_latest_value("HMI", "lit501_ai")

        reader.close()

        # Compute average, handle None values gracefully
        valid_tis = [val for val in ti_values if val is not None]
        ti_avg = sum(valid_tis) / len(valid_tis) if valid_tis else None

        msg = "📊 **System Status Update** 📊\n"
        msg += f"• Dewar Pressure: `{pt501:.2f}`\n" if pt501 is not None else "• `pt501_ai`: `No data`\n"
        msg += f"• Inlet Pressure: `{pt502:.2f}`\n" if pt502 is not None else "• `pt502_ai`: `No data`\n"
        msg += f"• Avg ColdHead Temp: `{ti_avg:.2f}`\n" if ti_avg is not None else "• Avg `ti50x_ai`: `No data`\n"
        msg += f"• Inlet FLow: `{fc501:.2f}`\n" if fc501 is not None else "• `fc501_ai`: `No data`\n"
        msg += f"• Outlet Flow: `{fc502:.2f}`\n" if fc502 is not None else "• `fc502_ai`: `No data`\n"
        msg += f"• Liquid Helium: `{lit501:.2f}%`\n" if lit501 is not None else "• `lit501_ai`: `No data`\n"
        msg += f"• ColdHead Alert: `{alert_triggered_ColdHeads}`\n"
        msg += f"• LN2 Alert: `{alert_triggered}`"

        await ctx.send(msg)

    except Exception as e:
        await ctx.send(f"❌ Failed to fetch system status: `{str(e)}`")





#DO NOT SHARE THIS TOKEN
try:
    bot.run('TOKEN')
finally:
    print("🛑 Bot is shutting down...")
